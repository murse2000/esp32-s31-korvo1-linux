// SPDX-License-Identifier: GPL-2.0-only
/* Native Korvo-1 RGB LCD framebuffer, LCD_CAM + reserved AXI TX0.
 * The reserved framebuffer stays at the bootloader's physical address so
 * early pixels survive handoff. Linux rebuilds the descriptor ring, clocks
 * and panel timings, and owns blank/unblank and resume from that point.
 */
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#define LCD_USER 0x14
#define LCD_MISC 0x18
#define LCD_UPDATE BIT(21)
#define LCD_START BIT(27)
#define LCD_RESET BIT(28)
#define FB_BYTES (800 * 480 * 2)
#define CHUNK 4032U
#define NODES DIV_ROUND_UP(FB_BYTES, CHUNK)
#define TX_CONF0 0x10
#define TX_CONF1 0x14
#define TX_LINK1 0x20
#define TX_LINK2 0x24
#define TX_PERIPH 0x44

struct s31fb {
    void __iomem *lcd, *tx, *clkrst, *descs;
    phys_addr_t desc_phys;
    struct gpio_desc *enable;
    u32 palette[16];
    bool blanked;
    int force_color;
    struct mutex lock;
};
static void s31fb_stop(struct fb_info *info)
{
    struct s31fb *p = info->par;
    writel((readl(p->lcd + LCD_USER) & ~LCD_START) | LCD_UPDATE, p->lcd + LCD_USER);
    writel(BIT(0), p->tx + TX_LINK1);
    udelay(50);
}
static void s31fb_ring(struct fb_info *info)
{
    struct s31fb *p = info->par;
    unsigned int offset = 0, i;
    for (i = 0; i < NODES; i++) {
        u32 size = min((unsigned int)FB_BYTES - offset, CHUNK);
        u32 ctl = size | (size << 12) | BIT(31);
        if (i == NODES - 1) ctl |= BIT(30);
        writel(ctl, p->descs + i * 16);
        writel(info->fix.smem_start + offset, p->descs + i * 16 + 4);
        writel(p->desc_phys + ((i + 1) % NODES) * 16, p->descs + i * 16 + 8);
        writel(0, p->descs + i * 16 + 12);
        offset += size;
    }
    wmb();
}
static void s31fb_start(struct fb_info *info)
{
    struct s31fb *p = info->par;
    writel(p->force_color < 0 ? 0 : ((u32)p->force_color << 1) | BIT(0), p->lcd + 0x40);
    s31fb_ring(info);
    writel(readl(p->lcd + LCD_USER) | LCD_RESET, p->lcd + LCD_USER);
    writel(readl(p->lcd + LCD_MISC) | BIT(27), p->lcd + LCD_MISC);
    writel(BIT(0), p->tx + TX_CONF0);
    writel(BIT(10) | BIT(3) | (3 << 5), p->tx + TX_CONF0);
    /* Leave owner checking off: TX descriptors form an endless ring. */
    writel(0, p->tx + TX_CONF1);
    writel(0, p->tx + TX_PERIPH);
    writel(0, p->tx + 8);
    writel(~0U, p->tx + 12);
    writel(p->desc_phys, p->tx + TX_LINK2);
    writel(BIT(1), p->tx + TX_LINK1);
    udelay(2);
    writel(readl(p->lcd + LCD_USER) | LCD_UPDATE, p->lcd + LCD_USER);
    writel(readl(p->lcd + LCD_USER) | LCD_START, p->lcd + LCD_USER);
    gpiod_set_value_cansleep(p->enable, 1);
}
static int s31fb_check_var(struct fb_var_screeninfo *v, struct fb_info *info)
{
    if (v->xres != 800 || v->yres != 480 || v->bits_per_pixel != 16 ||
        v->xres_virtual != 800 || v->yres_virtual != 480 ||
        v->xoffset || v->yoffset || v->rotate ||
        v->pixclock < 50000 || v->pixclock > 100000 ||
        !v->hsync_len || v->hsync_len > 128 || !v->vsync_len || v->vsync_len > 1024 ||
        v->left_margin + v->right_margin > 1000 || v->upper_margin + v->lower_margin > 1000)
        return -EINVAL;
    v->red = (struct fb_bitfield){11, 5, 0};
    v->green = (struct fb_bitfield){5, 6, 0};
    v->blue = (struct fb_bitfield){0, 5, 0};
    v->transp = (struct fb_bitfield){0, 0, 0};
    return 0;
}
static int s31fb_set_par(struct fb_info *info)
{
    struct s31fb *p = info->par;
    struct fb_var_screeninfo *v = &info->var;
    u32 target = 1000000000U / v->pixclock * 1000U;
    unsigned int a, b, n = 160000000U / target, best_a = 1, best_b = 0;
    u32 error = ~0U, actual = target;
    for (a = 1; a < 256; a++) {
        b = div_u64((160000000ULL - (u64)n * target) * a + target / 2, target);
        if (b >= a) continue;
        u32 hz = div_u64(160000000ULL * a, n * a + b);
        u32 e = abs((int)hz - (int)target);
        if (e < error) { error = e; best_a = a; best_b = b; actual = hz; }
    }
    mutex_lock(&p->lock);
    s31fb_stop(info);
    /* Only enable common bus gates; never reset the camera or AXI block. */
    writel(readl(p->clkrst + 0x14c) | BIT(0) | BIT(2), p->clkrst + 0x14c);
    writel(readl(p->clkrst + 0x154) | BIT(2), p->clkrst + 0x154);
    writel(1 | BIT(2) | ((n - 1) << 3) | (best_b << 11) | (best_a << 19), p->clkrst + 0x150);
    writel(BIT(31) | BIT(8) | BIT(6) | 1, p->lcd); /* PCLK on negative edge */
    writel(0, p->lcd + 0x10); /* RGB565, converter bypass */
    writel(BIT(13) | (1 << 19) | BIT(24), p->lcd + LCD_USER);
    writel(BIT(3) | BIT(4) | BIT(25), p->lcd + LCD_MISC);
    writel((480 - 1) | ((v->vsync_len + v->upper_margin + 480 + v->lower_margin - 1) << 16), p->lcd + 0x1c);
    writel((800 - 1) | ((v->hsync_len + v->left_margin + 800 + v->right_margin - 1) << 16), p->lcd + 0x20);
    writel((v->hsync_len + v->left_margin - 1) |
          ((v->vsync_len + v->upper_margin - 1) << 16), p->lcd + 0x24);
    writel((v->vsync_len - 1) | BIT(12) | ((v->hsync_len - 1) << 16) |
          ((v->sync & FB_SYNC_VERT_HIGH_ACT) ? 0 : BIT(10)) |
          ((v->sync & FB_SYNC_HOR_HIGH_ACT) ? 0 : BIT(23)), p->lcd + 0x28);
    writel(0, p->lcd + 0x34); writel(0, p->lcd + 0x38);
    writel(0, p->lcd + 0x3c); /* no transfer buffering */
    v->pixclock = div_u64(1000000000000ULL, actual);
    if (!p->blanked) s31fb_start(info);
    mutex_unlock(&p->lock);
    return 0;
}
static int s31fb_blank(int blank, struct fb_info *info)
{
    struct s31fb *p = info->par;
    mutex_lock(&p->lock);
    p->blanked = blank != FB_BLANK_UNBLANK;
    if (p->blanked) {
        /* This panel has a fixed backlight. Transmit one black frame
         * before stopping scanout so the visible image is actually blank.
         */
        writel(BIT(0), p->lcd + 0x40);
        msleep(50);
        gpiod_set_value_cansleep(p->enable, 0);
        s31fb_stop(info);
    } else s31fb_start(info);
    mutex_unlock(&p->lock);
    return 0;
}
static int s31fb_setcolreg(unsigned int index, unsigned int r, unsigned int g,
                         unsigned int b, unsigned int t, struct fb_info *info)
{
    struct s31fb *p = info->par;
    if (index >= 16) return -EINVAL;
    p->palette[index] = ((r >> 11) << 11) | ((g >> 10) << 5) | (b >> 11);
    return 0;
}
static const struct fb_ops s31fb_ops = {
    .owner = THIS_MODULE,
    FB_DEFAULT_IOMEM_OPS,
    .fb_check_var = s31fb_check_var, .fb_set_par = s31fb_set_par,
    .fb_blank = s31fb_blank, .fb_setcolreg = s31fb_setcolreg,
};
static ssize_t force_color_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct fb_info *info = dev_get_drvdata(dev);
    struct s31fb *p = info->par;
    return sysfs_emit(buf, "%d\n", p->force_color);
}
static ssize_t force_color_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct fb_info *info = dev_get_drvdata(dev);
    struct s31fb *p = info->par;
    int color, ret = kstrtoint(buf, 0, &color);
    if (ret) return ret;
    if (color < -1 || color > 65535) return -EINVAL;
    mutex_lock(&p->lock);
    p->force_color = color;
    if (!p->blanked) writel(color < 0 ? 0 : ((u32)color << 1) | BIT(0), p->lcd + 0x40);
    mutex_unlock(&p->lock);
    return count;
}
static DEVICE_ATTR_RW(force_color);
static struct attribute *s31fb_attrs[] = { &dev_attr_force_color.attr, NULL };
ATTRIBUTE_GROUPS(s31fb);
static int s31fb_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct fb_info *info;
    struct s31fb *p;
    struct device_node *mem;
    struct resource frame, desc, *reg;
    int ret;
    info = framebuffer_alloc(sizeof(*p), dev);
    if (!info) return -ENOMEM;
    p = info->par;
    p->force_color = -1;
    mutex_init(&p->lock);
    reg = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    p->lcd = devm_ioremap(dev, reg->start, resource_size(reg));
    p->tx = devm_platform_ioremap_resource(pdev, 1);
    reg = platform_get_resource(pdev, IORESOURCE_MEM, 2);
    p->clkrst = devm_ioremap(dev, reg->start, resource_size(reg));
    if (!p->lcd || !p->clkrst || IS_ERR(p->tx)) { ret = -ENOMEM; goto release; }
    mem = of_parse_phandle(dev->of_node, "memory-region", 0);
    ret = mem ? of_address_to_resource(mem, 0, &frame) : -EINVAL;
    of_node_put(mem);
    if (ret) goto release;
    mem = of_parse_phandle(dev->of_node, "memory-region", 1);
    ret = mem ? of_address_to_resource(mem, 0, &desc) : -EINVAL;
    of_node_put(mem);
    if (ret) goto release;
    if (resource_size(&frame) < FB_BYTES || resource_size(&desc) < NODES * 16) { ret = -EINVAL; goto release; }
    p->desc_phys = desc.start;
    p->descs = devm_ioremap(dev, desc.start, resource_size(&desc));
    p->enable = devm_gpiod_get(dev, "enable", GPIOD_OUT_HIGH);
    if (IS_ERR(p->enable)) { ret = PTR_ERR(p->enable); goto release; }
    info->screen_base = devm_ioremap_wc(dev, frame.start, FB_BYTES);
    if (!p->descs || !info->screen_base) { ret = -ENOMEM; goto release; }
    info->screen_size = FB_BYTES;
    info->fix = (struct fb_fix_screeninfo){.id = "esp32s31-rgb", .smem_start = frame.start,
        .smem_len = FB_BYTES, .type = FB_TYPE_PACKED_PIXELS, .visual = FB_VISUAL_TRUECOLOR,
        .line_length = 1600, .accel = FB_ACCEL_NONE};
    info->var = (struct fb_var_screeninfo){.xres = 800, .yres = 480,
        .xres_virtual = 800, .yres_virtual = 480, .bits_per_pixel = 16,
        .pixclock = 55556, .left_margin = 40, .right_margin = 48,
        .upper_margin = 32, .lower_margin = 13, .hsync_len = 40, .vsync_len = 23,
        .sync = 0, .activate = FB_ACTIVATE_NOW};
    s31fb_check_var(&info->var, info);
    info->pseudo_palette = p->palette;
    info->fbops = &s31fb_ops;
    platform_set_drvdata(pdev, info);
    s31fb_set_par(info);
    ret = register_framebuffer(info);
    if (ret) goto release;
    dev_info(dev, "native RGB565 800x480 framebuffer, timing/DMA/blank control, %u ps pixel clock\n", info->var.pixclock);
    return 0;
release:
    framebuffer_release(info);
    return ret;
}
static int s31fb_suspend(struct device *dev) { return s31fb_blank(FB_BLANK_POWERDOWN, dev_get_drvdata(dev)); }
static int s31fb_resume(struct device *dev) { return s31fb_blank(FB_BLANK_UNBLANK, dev_get_drvdata(dev)); }
static DEFINE_SIMPLE_DEV_PM_OPS(s31fb_pm, s31fb_suspend, s31fb_resume);
static const struct of_device_id s31fb_match[] = {
    {.compatible = "espressif,esp32s31-korvo1-rgb"}, {}
};
static struct platform_driver s31fb_driver = {
    .probe = s31fb_probe,
    .driver = {.name = "esp32s31-fb", .of_match_table = s31fb_match,
               .pm = pm_sleep_ptr(&s31fb_pm), .dev_groups = s31fb_groups,
               .suppress_bind_attrs = true},
};
builtin_platform_driver(s31fb_driver);
