// SPDX-License-Identifier: GPL-2.0-only
/* ESP32-S31 PPA / 2D-DMA. Register programming follows Espressif ESP-IDF
 * esp32s31 ppa_ll.h, dma2d_ll.h and ppa_fill.c (Apache-2.0).
 * AXI GDMA used by LCD/camera and AHB GDMA used by audio are untouched.
 */
#include <linux/dma-mapping.h>
#include <linux/dma-map-ops.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/ktime.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include "korvo_ppa.h"

#define OUT(ch) ((ch) * 0x100)
#define IN(ch) (0x500 + (ch) * 0x100)
#define CONF 0
#define RAW 4
#define ENA 8
#define CLR 0x10
#define LINK 0x1c
#define ADDR 0x20
#define OUT_PERI 0x38
#define IN_PERI 0x3c
#define CH_RESET BIT(24)
#define CH_DISABLE BIT(25)
#define CH_CONFIG (BIT(2) | (4 << 6) | (3 << 9) | BIT(12))
#define DESC_BASE 0x2f079e00U
#define DESC_STRIDE 32
#define DESC_RX 0
#define DESC_TX 32
#define DESC_FG 64
#define FB_PHYS 0x50f40000U
#define FB_BYTES (800 * 480 * 2)

struct s31ppa {
    struct device *dev;
    void __iomem *ppa, *dma, *clock, *system, *descs;
    u32 desc_address;
    void *buffer[2];
    size_t buffer_size[2];
    bool reserved_canvas;
    dma_addr_t address[2];
    struct miscdevice misc;
    struct mutex lock;
    struct file *owner;
    struct korvo_ppa_stats stats;
};
static struct platform_device *fallback_device;

static void clock_enable(struct s31ppa *p)
{
    unsigned int offset;
    for (offset = 0xb0; offset <= 0xb4; offset += 4) {
        u32 value = readl(p->clock + offset) | BIT(0);
        writel(value | BIT(1), p->clock + offset);
        writel(value & ~BIT(1), p->clock + offset);
    }
    /* Force only the PPA and DMA2D SRAM blocks on. */
    writel((readl(p->system + 0x210) | BIT(3)) & ~BIT(2), p->system + 0x210);
    writel((readl(p->system + 0x29c) | BIT(3)) & ~BIT(2), p->system + 0x29c);
    writel(BIT(2) | BIT(0) | BIT(1), p->dma + 0xa04);
    writel(BIT(2), p->dma + 0xa04);
    writel(0, p->dma + 0xa18);
    writel(0, p->dma + 0xa1c);
    writel(0x2f000000, p->dma + 0xa08);
    writel(0x2f080000, p->dma + 0xa0c);
    writel(0x50000000, p->dma + 0xa10);
    writel(0x51000000, p->dma + 0xa14);
}
static void channel(struct s31ppa *p, bool input, unsigned int ch,
                    unsigned int peripheral, bool memory, unsigned int descriptor)
{
    unsigned int base = input ? IN(ch) : OUT(ch);
    u32 config = CH_CONFIG | (input ? (memory ? BIT(0) : 0) : BIT(1));
    writel(config | CH_DISABLE, p->dma + base + CONF);
    writel(config | CH_DISABLE | CH_RESET, p->dma + base + CONF);
    writel(config, p->dma + base + CONF);
    writel(0, p->dma + base + ENA);
    writel(~0U, p->dma + base + CLR);
    /* No reorder or color conversion for these pixel streams. */
    /* CSC input selector 7 bypasses conversion. Zero enables conversion. */
    writel((7 << 3) | (input ? 1 : 2), p->dma + base + (input ? 0x4c : 0x48));
    writel(peripheral, p->dma + base + (input ? IN_PERI : OUT_PERI));
    writel(p->desc_address + descriptor, p->dma + base + ADDR);
}
static void descriptor(struct s31ppa *p, unsigned int offset, u32 address,
                        u32 picture_w, u32 picture_h, u32 x, u32 y,
                        u32 width, u32 height, u32 pbyte)
{
    writel(height | (width << 14) | BIT(29) | BIT(30) | BIT(31), p->descs + offset);
    writel(picture_h | (picture_w << 14) | (pbyte << 28), p->descs + offset + 4);
    writel(y | (x << 14), p->descs + offset + 8);
    writel(address, p->descs + offset + 12);
    writel(0, p->descs + offset + 16);
    writel(0, p->descs + offset + 20);
}
static int finish(struct s31ppa *p)
{
    u32 status;
    /* A tile completes in microseconds. Sleeping for 1us here can become
     * a full 10ms scheduler tick on this kernel, once per draw operation.
     * Poll briefly, then yield only for an unusually long transfer.
     */
    int ret = readl_poll_timeout(p->dma + IN(0) + RAW, status,
                               status & GENMASK(4, 1), 0, 2000);
    if (ret) ret = readl_poll_timeout(p->dma + IN(0) + RAW, status,
                                     status & GENMASK(4, 1), 10, 20000);
    if (!ret && !(status & BIT(1))) ret = -EIO;
    if (ret) {
        dev_err_ratelimited(p->dev, "2D transfer failed %d RX=%08x TX=%08x AXI=%08x\n",
                           ret, status, readl(p->dma + OUT(0) + RAW), readl(p->dma + 0xa00));
        p->stats.errors++;
        clock_enable(p);
    }
    rmb();
    return ret;
}
static int surface(struct s31ppa *p, const struct korvo_ppa_surface *s,
                   u32 x, u32 y, u32 w, u32 h, u32 bytes, bool framebuffer, u32 *address)
{
    u64 length = (u64)s->width * s->height * bytes;
    if (!w || !h || !s->width || !s->height || s->width > 16383 || s->height > 16383 ||
        x >= s->width || y >= s->height || w > s->width - x || h > s->height - y ||
        (s->offset & 3)) return -EINVAL;
    if (s->buffer == KORVO_PPA_FRAMEBUFFER) {
        if (!framebuffer || s->offset || s->width != 800 || s->height != 480 || bytes != 2)
            return -EINVAL;
        *address = FB_PHYS;
    } else {
        if (s->buffer > 1 || s->offset >= p->buffer_size[s->buffer] ||
            length > p->buffer_size[s->buffer] - s->offset) return -EINVAL;
        *address = p->address[s->buffer] + s->offset;
    }
    return 0;
}
static u32 expand_rgb565(u32 color)
{
    u32 r = (color >> 11) & 31, g = (color >> 5) & 63, b = color & 31;
    return 0xff000000 | (((r << 3) | (r >> 2)) << 16) |
           (((g << 2) | (g >> 4)) << 8) | (b << 3) | (b >> 2);
}
static void sync_surface(struct s31ppa *p, const struct korvo_ppa_surface *s,
                         u32 x, u32 y, u32 w, u32 h, u32 bytes, bool device)
{
    unsigned int offset = s->offset + (y * s->width + x) * bytes;
    unsigned int length = ((h - 1) * s->width + w) * bytes;
    if(device) dma_sync_single_range_for_device(p->dev, p->address[s->buffer],
                                                offset, length, DMA_BIDIRECTIONAL);
    else dma_sync_single_range_for_cpu(p->dev, p->address[s->buffer],
                                        offset, length, DMA_BIDIRECTIONAL);
}
static int run(struct s31ppa *p, const struct korvo_ppa_request *q)
{
    u32 src = 0, dst, src_bytes = q->format == KORVO_PPA_ARGB8888 ? 4 : 2;
    int ret;
    u64 start;
    bool fill = q->operation == KORVO_PPA_FILL;
    bool flush = q->operation == KORVO_PPA_FLUSH;
    bool blend = q->operation == KORVO_PPA_BLEND;
    if (q->operation != KORVO_PPA_COPY && !fill && !flush && !blend) return -EOPNOTSUPP;
    if (q->format > KORVO_PPA_ARGB8888 || (q->format && !blend) || q->alpha > 255)
        return -EOPNOTSUPP;
    /* DMA2D RX needs complete 32-bit writes. Clients fall back for odd edges. */
    if ((q->destination.width | q->destination_x | q->width) & 1) return -EINVAL;
    if ((ret = surface(p, &q->destination, q->destination_x, q->destination_y,
                       q->width, q->height, 2, flush, &dst))) return ret;
    if (q->destination.buffer == KORVO_PPA_FRAMEBUFFER && !flush) return -EINVAL;
    if (!fill && (ret = surface(p, &q->source, q->source_x, q->source_y,
                                q->width, q->height, src_bytes, false, &src))) return ret;
    if (!fill && q->source.buffer == q->destination.buffer) return -EINVAL;
    start = ktime_get_ns();
    if (!fill) sync_surface(p, &q->source, q->source_x, q->source_y,
                            q->width, q->height, src_bytes, true);
    if (q->destination.buffer < 2)
        sync_surface(p, &q->destination, q->destination_x, q->destination_y,
                      q->width, q->height, 2, true);
    descriptor(p, DESC_RX, dst, q->destination.width, q->destination.height,
               q->destination_x, q->destination_y, q->width, q->height, 3);
    if (fill || blend) {
        writel(BIT(4), p->ppa + 0x34);
        writel(0, p->ppa + 0x34);
        channel(p, true, 0, 2, false, DESC_RX);
        writel(0, p->ppa + 0x2c);
        if (blend) {
            u32 fg_mode = q->format == KORVO_PPA_ARGB8888 ? 0 : 2;
            u32 fg_alpha_mode = q->format == KORVO_PPA_ARGB8888 ? (q->alpha == 255 ? 0 : 2) : 1;
            descriptor(p, DESC_TX, dst, q->destination.width, q->destination.height,
                       q->destination_x, q->destination_y, q->width, q->height, 3);
            descriptor(p, DESC_FG, src, q->source.width, q->source.height,
                       q->source_x, q->source_y, q->width, q->height, src_bytes == 4 ? 5 : 3);
            channel(p, false, 0, 2, false, DESC_TX);
            channel(p, false, 1, 3, false, DESC_FG);
            writel(2 | (fg_mode << 4) | (2 << 8), p->ppa + 0x24);
            writel(255 | (q->alpha << 8) | BIT(16) | (fg_alpha_mode << 18), p->ppa + 0x40);
            /* Empty color-key ranges: all pixels use normal alpha blending. */
            writel(0xffffff, p->ppa + 0x50); writel(0, p->ppa + 0x54);
            writel(0xffffff, p->ppa + 0x58); writel(0, p->ppa + 0x5c);
            writel(0, p->ppa + 0x60);
            wmb();
            writel(BIT(21), p->dma + OUT(0) + LINK);
            writel(BIT(21), p->dma + OUT(1) + LINK);
        } else {
            writel(2 << 8, p->ppa + 0x24);
            writel(expand_rgb565(q->color), p->ppa + 0x4c);
        }
        wmb();
        writel(BIT(22), p->dma + IN(0) + LINK);
        writel(q->width | (q->height << 14), p->ppa + 0x3c);
        /* Enable first, then strobe UPDATE in a separate bus transaction. */
        writel(BIT(0) | (fill ? BIT(2) : 0), p->ppa + 0x34);
        writel(BIT(0) | (fill ? BIT(2) : 0) | BIT(3), p->ppa + 0x34);
    } else {
        descriptor(p, DESC_TX, src, q->source.width, q->source.height,
                   q->source_x, q->source_y, q->width, q->height, 3);
        channel(p, false, 0, 4, true, DESC_TX);
        channel(p, true, 0, 3, true, DESC_RX);
        wmb();
        writel(BIT(21), p->dma + OUT(0) + LINK);
        writel(BIT(22), p->dma + IN(0) + LINK);
    }
    ret = finish(p);
    if (q->destination.buffer < 2)
        sync_surface(p, &q->destination, q->destination_x, q->destination_y,
                      q->width, q->height, 2, false);
    if (!fill) sync_surface(p, &q->source, q->source_x, q->source_y,
                            q->width, q->height, src_bytes, false);
    p->stats.nanoseconds += ktime_get_ns() - start;
    if (!ret) {
        if (fill) p->stats.fills++;
        else if (blend) p->stats.blends++;
        else if (flush) p->stats.flushes++;
        else p->stats.copies++;
        p->stats.bytes += q->width * q->height * 2;
    }
    return ret;
}
static int claim(struct s31ppa *p, struct file *file)
{
    if (p->owner && p->owner != file) return -EBUSY;
    p->owner = file;
    return 0;
}
static long ppa_ioctl(struct file *file, unsigned int command, unsigned long arg)
{
    struct s31ppa *p = container_of(file->private_data, struct s31ppa, misc);
    void __user *user = (void __user *)arg;
    struct korvo_ppa_request q;
    int ret;
    if (command == KORVO_PPA_IOC_INFO) {
        struct korvo_ppa_info info = {KORVO_PPA_ABI, KORVO_PPA_BUFFER_SIZE, 2,
            BIT(KORVO_PPA_COPY) | BIT(KORVO_PPA_FILL) | BIT(KORVO_PPA_FLUSH) |
            BIT(KORVO_PPA_BLEND), 800, 480, p->buffer_size[0], 0};
        return copy_to_user(user, &info, sizeof(info)) ? -EFAULT : 0;
    }
    if (command == KORVO_PPA_IOC_STATS) {
        struct korvo_ppa_stats stats;
        mutex_lock(&p->lock); stats = p->stats; mutex_unlock(&p->lock);
        return copy_to_user(user, &stats, sizeof(stats)) ? -EFAULT : 0;
    }
    if (command != KORVO_PPA_IOC_RUN) return -ENOTTY;
    if (copy_from_user(&q, user, sizeof(q))) return -EFAULT;
    if (mutex_lock_interruptible(&p->lock)) return -ERESTARTSYS;
    ret = claim(p, file);
    if (!ret) ret = run(p, &q);
    mutex_unlock(&p->lock);
    return ret;
}
static int ppa_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct s31ppa *p = container_of(file->private_data, struct s31ppa, misc);
    unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
    unsigned long length = vma->vm_end - vma->vm_start;
    unsigned int index;
    int ret;
    if (offset != 0 && offset != KORVO_PPA_BUFFER_SIZE)
        return -EINVAL;
    index = offset / KORVO_PPA_BUFFER_SIZE;
    if (length > p->buffer_size[index]) return -EINVAL;
    mutex_lock(&p->lock);
    ret = claim(p, file);
    if (!ret) {
        dma_sync_single_for_cpu(p->dev, p->address[index],
                                p->buffer_size[index], DMA_BIDIRECTIONAL);
        vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
        ret = remap_pfn_range(vma, vma->vm_start, virt_to_phys(p->buffer[index]) >> PAGE_SHIFT,
                             length, vma->vm_page_prot);
    }
    mutex_unlock(&p->lock);
    return ret;
}
static int ppa_release(struct inode *inode, struct file *file)
{
    struct s31ppa *p = container_of(file->private_data, struct s31ppa, misc);
    mutex_lock(&p->lock);
    if (p->owner == file) p->owner = NULL;
    mutex_unlock(&p->lock);
    return 0;
}
static const struct file_operations ppa_fops = {
    .owner = THIS_MODULE, .unlocked_ioctl = ppa_ioctl, .compat_ioctl = ppa_ioctl,
    .mmap = ppa_mmap, .release = ppa_release,
};
static void buffers_free(void *data)
{
    struct s31ppa *p = data;
    int i;
    for (i = 0; i < 2; i++) if (p->buffer[i]) {
        dma_unmap_single(p->dev, p->address[i], p->buffer_size[i], DMA_BIDIRECTIONAL);
        if (i || !p->reserved_canvas)
            free_pages((unsigned long)p->buffer[i], get_order(p->buffer_size[i]));
    }
}
static int ppa_probe(struct platform_device *pdev)
{
    struct s31ppa *p = devm_kzalloc(&pdev->dev, sizeof(*p), GFP_KERNEL);
    int i, ret;
    if (!p) return -ENOMEM;
    p->dev = &pdev->dev;
    p->desc_address = DESC_BASE;
    p->buffer_size[0] = p->buffer_size[1] = KORVO_PPA_BUFFER_SIZE;
    if (p->dev->of_node) {
        struct device_node *node = of_parse_phandle(p->dev->of_node, "memory-region", 0);
        struct resource mem;
        if (!node) return -EINVAL;
        ret = of_address_to_resource(node, 0, &mem);
        of_node_put(node);
        if (ret || mem.start != DESC_BASE || resource_size(&mem) < 0x200) return -EINVAL;
        p->desc_address = mem.start;
    }
    mutex_init(&p->lock);
    /* Non-OF platform devices otherwise inherit coherent=true on RISC-V.
     * PPA and 2D-DMA are not coherent with the S31 D-cache.
     */
    dev_assign_dma_coherent(p->dev, false);
    dev_clear_dma_skip_sync(p->dev);
    ret = dma_set_mask_and_coherent(p->dev, DMA_BIT_MASK(32));
    if (ret) return ret;
    p->ppa = devm_ioremap_resource(p->dev, platform_get_resource(pdev, IORESOURCE_MEM, 0));
    p->dma = devm_ioremap_resource(p->dev, platform_get_resource(pdev, IORESOURCE_MEM, 1));
    p->clock = devm_ioremap(p->dev, 0x20587000, 0xc0);
    p->system = devm_ioremap(p->dev, 0x20586000, 0x300);
    p->descs = devm_ioremap(p->dev, p->desc_address, 0x200);
    if (IS_ERR(p->ppa) || IS_ERR(p->dma) || !p->clock || !p->system || !p->descs) return -ENOMEM;
    if (p->dev->of_node) {
        struct device_node *node = of_parse_phandle(p->dev->of_node, "espressif,canvas-memory", 0);
        if (node) {
            struct resource mem;
            ret = of_address_to_resource(node, 0, &mem);
            of_node_put(node);
            if (ret || mem.start != 0x50db8000 || resource_size(&mem) != 0xc0000) return -EINVAL;
            p->buffer[0] = devm_memremap(p->dev, mem.start, resource_size(&mem), MEMREMAP_WB);
            if (IS_ERR(p->buffer[0])) return PTR_ERR(p->buffer[0]);
            p->reserved_canvas = true;
            p->buffer_size[0] = resource_size(&mem);
        }
    }
    for (i = 0; i < 2; i++) {
        void *buffer = p->buffer[i];
        if (!buffer) buffer = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
                                                       get_order(p->buffer_size[i]));
        if (!buffer) { ret = -ENOMEM; goto free_buffers; }
        p->address[i] = dma_map_single(p->dev, buffer, p->buffer_size[i], DMA_BIDIRECTIONAL);
        if (dma_mapping_error(p->dev, p->address[i])) {
            if (i || !p->reserved_canvas) free_pages((unsigned long)buffer, get_order(p->buffer_size[i]));
            p->buffer[i] = NULL;
            ret = -EIO; goto free_buffers;
        }
        p->buffer[i] = buffer;
    }
    ret = devm_add_action_or_reset(p->dev, buffers_free, p);
    if (ret) return ret;
    clock_enable(p);
    p->misc = (struct miscdevice){.minor = MISC_DYNAMIC_MINOR, .name = "esp32s31-ppa",
        .fops = &ppa_fops, .parent = p->dev, .mode = 0600};
    platform_set_drvdata(pdev, p);
    ret = misc_register(&p->misc);
    if (ret) return ret;
    dev_info(p->dev, "PPA + DMA2D, cached render=%zu scratch=%zu, descriptors %08x\n",
             p->buffer_size[0], p->buffer_size[1], p->desc_address);
    return 0;
free_buffers:
    buffers_free(p);
    return ret;
}
static void ppa_remove(struct platform_device *pdev)
{
    struct s31ppa *p = platform_get_drvdata(pdev);
    misc_deregister(&p->misc);
}
static const struct of_device_id ppa_matches[] = {
    {.compatible = "espressif,esp32s31-ppa"}, {}
};
MODULE_DEVICE_TABLE(of, ppa_matches);
static struct platform_driver ppa_driver = {
    .probe = ppa_probe, .remove = ppa_remove,
    .driver = {.name = "esp32s31-ppa", .of_match_table = ppa_matches, .suppress_bind_attrs = true},
};
static int __init ppa_init(void)
{
    struct device_node *node;
    int ret = platform_driver_register(&ppa_driver);
    if (ret) return ret;
    node = of_find_compatible_node(NULL, NULL, "espressif,esp32s31-ppa");
    if (node) { of_node_put(node); return 0; }
    /* Compatibility with the already-flashed Korvo DT. The tail of the
     * bootloader's 4KiB descriptor reservation is unused by camera (464B).
     */
    if (of_machine_is_compatible("esp,esp32s31-korvo-1")) {
        const struct resource resources[] = {
            DEFINE_RES_MEM(0x20345000, 0x1000), DEFINE_RES_MEM(0x20346000, 0x1000)
        };
        fallback_device = platform_device_register_simple("esp32s31-ppa", -1,
                                                          resources, ARRAY_SIZE(resources));
        if (IS_ERR(fallback_device)) {
            ret = PTR_ERR(fallback_device); fallback_device = NULL;
            platform_driver_unregister(&ppa_driver);
            return ret;
        }
    }
    return 0;
}
static void __exit ppa_exit(void)
{
    if (fallback_device) platform_device_unregister(fallback_device);
    platform_driver_unregister(&ppa_driver);
}
module_init(ppa_init);
module_exit(ppa_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ESP32-S31 PPA and DMA2D pixel acceleration");
