// SPDX-License-Identifier: GPL-2.0-only
/* Korvo-1 LCD_CAM DVP receiver, fixed OV3660 RGB565 240x240 mode.
 * AXI GDMA RX1 is exclusively reserved by this board driver. LCD TX0
 * remains owned by the bootloader. No shared LCD/AXI global reset is used.
 * Register fields and sensor setup follow Espressif ESP-IDF/esp-video.
 */
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-vmalloc.h>
#include "ov3660-korvo-regs.h"
#include "sc101iot-korvo-regs.h"

#define WIDTH 240
#define HEIGHT 240
#define FRAME_BYTES (WIDTH * HEIGHT * 2)
#define DESC_BYTES 4032U
#define DESC_COUNT DIV_ROUND_UP(FRAME_BYTES, DESC_BYTES)
#define CAM_UPDATE BIT(4)
#define CAM_VS_EOF BIT(8)
#define CAM_START BIT(29)
#define CAM_RESET BIT(30)
#define CAM_FIFO_RESET BIT(31)
#define RX_RAW 0x00
#define RX_ENA 0x08
#define RX_CLEAR 0x0c
#define RX_CONF0 0x10
#define RX_CONF1 0x14
#define RX_LINK1 0x20
#define RX_LINK2 0x24
#define RX_PERIPH 0x44
#define RX_START BIT(2)
#define RX_STOP BIT(1)
#define RX_PARK BIT(4)
#define RX_ABORT BIT(7)
#define RX_ERRORS (BIT(2) | BIT(3) | BIT(4) | BIT(7) | BIT(8))
static bool invert_vsync = true;
module_param(invert_vsync, bool, 0644);
MODULE_PARM_DESC(invert_vsync, "Invert VSYNC as in Espressif's DVP input routing");

struct hw_desc { __le32 ctl, buffer, next, pad; };
struct capture_buffer { struct vb2_v4l2_buffer vb; struct list_head node; };
struct korvo_cam {
    struct device *dev;
    void __iomem *cam, *rx, *clock;
    struct i2c_client *sensor;
    struct v4l2_device v4l2;
    struct video_device video;
    struct vb2_queue queue;
    struct mutex lock;
    spinlock_t pending_lock;
    struct list_head pending;
    wait_queue_head_t wait;
    struct task_struct *thread;
    void __iomem *desc;
    dma_addr_t desc_dma, frame_dma[DESC_COUNT];
    void *frame[DESC_COUNT];
    u32 sequence;
    bool sc101iot;
};
static int sensor_write8(struct korvo_cam *c, u8 reg, u8 val)
{
    u8 data[] = {reg, val};
    int ret = i2c_master_send(c->sensor, data, 2);
    return ret == 2 ? 0 : ret < 0 ? ret : -EIO;
}
static int sensor_read8(struct korvo_cam *c, u8 reg)
{
    u8 val;
    struct i2c_msg msgs[] = {
        {.addr = c->sensor->addr, .len = 1, .buf = &reg},
        {.addr = c->sensor->addr, .flags = I2C_M_RD, .len = 1, .buf = &val},
    };
    int ret = i2c_transfer(c->sensor->adapter, msgs, 2);
    return ret == 2 ? val : ret < 0 ? ret : -EIO;
}
static int sensor_write(struct korvo_cam *c, u16 reg, u8 val)
{
    u8 data[] = {reg >> 8, reg & 0xff, val};
    int ret = i2c_master_send(c->sensor, data, sizeof(data));
    return ret == sizeof(data) ? 0 : ret < 0 ? ret : -EIO;
}
static int sensor_read(struct korvo_cam *c, u16 reg)
{
    u8 addr[] = {reg >> 8, reg & 0xff}, val;
    struct i2c_msg msgs[] = {
        {.addr = c->sensor->addr, .len = 2, .buf = addr},
        {.addr = c->sensor->addr, .flags = I2C_M_RD, .len = 1, .buf = &val},
    };
    int ret = i2c_transfer(c->sensor->adapter, msgs, 2);
    return ret == 2 ? val : ret < 0 ? ret : -EIO;
}
static int sensor_setup(struct korvo_cam *c)
{
    int i, ret;
    if (c->sc101iot) {
        static const u8 crop[][2] = {
            {0xf0, 0x01}, {0x70, 0x08}, {0x71, 0xf8}, {0x72, 0x22},
            {0x73, 0xf0}, {0x74, 0xe0}, {0x75, 0x10},
        };
        for (i = 0; i < ARRAY_SIZE(sc101iot_setup); i++) {
            ret = sensor_write8(c, sc101iot_setup[i].reg, sc101iot_setup[i].val);
            if (ret) return ret;
        }
        msleep(50);
        for (i = 0; i < ARRAY_SIZE(crop); i++) {
            ret = sensor_write8(c, crop[i][0], crop[i][1]);
            if (ret) return ret;
        }
        msleep(20);
        return 0;
    }
    for (i = 0; i < ARRAY_SIZE(ov3660_setup); i++) {
        if (ov3660_setup[i].reg == 0xffff) {
            msleep(ov3660_setup[i].val);
            continue;
        }
        ret = sensor_write(c, ov3660_setup[i].reg, ov3660_setup[i].val);
        if (ret) return ret;
    }
    return sensor_write(c, 0x3008, 0x02);
}
static void dma_stop(struct korvo_cam *c)
{
    u32 ctrl = readl(c->cam + 4) & ~CAM_START;
    int i;
    writel(ctrl, c->cam + 4);
    writel(readl(c->cam) | CAM_UPDATE, c->cam);
    writel(readl(c->rx + RX_CONF0) | RX_ABORT, c->rx + RX_CONF0);
    writel(RX_STOP, c->rx + RX_LINK1);
    for (i = 0; i < 1000 && !(readl(c->rx + RX_LINK1) & RX_PARK); i++) udelay(1);
    writel(BIT(0), c->rx + RX_CONF0);
    writel(0, c->rx + RX_CONF0);
    writel(0, c->rx + RX_ENA);
    writel(~0U, c->rx + RX_CLEAR);
}
static int capture_one(struct korvo_cam *c)
{
    unsigned int i, remaining = FRAME_BYTES, offset = 0, received = 0;
    u32 status = 0;
    dma_stop(c);
    for (i = 0; i < DESC_COUNT; i++) {
        unsigned int size = min(remaining, DESC_BYTES);
        writel(BIT(31) | size, c->desc + i * 16);
        writel(c->frame_dma[i], c->desc + i * 16 + 4);
        writel(i + 1 < DESC_COUNT ? c->desc_dma + (i + 1) * 16 : 0, c->desc + i * 16 + 8);
        writel(0, c->desc + i * 16 + 12);
        offset += size;
        remaining -= size;
    }
    dma_wmb();
    writel(BIT(9) | (2 << 4), c->rx + RX_CONF0); /* 32-byte burst */
    writel(BIT(12), c->rx + RX_CONF1);
    writel(0, c->rx + RX_PERIPH); /* CAM0 trigger */
    writel(c->desc_dma, c->rx + RX_LINK2);
    writel(RX_START, c->rx + RX_LINK1);
    writel(CAM_RESET | CAM_FIFO_RESET, c->cam + 4);
    writel(invert_vsync ? BIT(27) : 0, c->cam + 4);
    writel(CAM_VS_EOF | CAM_UPDATE, c->cam);
    writel(CAM_START | (invert_vsync ? BIT(27) : 0), c->cam + 4);
    for (i = 0; i < 200 && !kthread_should_stop(); i++) {
        status = readl(c->rx + RX_RAW);
        if (status & (BIT(1) | RX_ERRORS)) break;
        usleep_range(5000, 6000);
    }
    dma_stop(c);
    dma_rmb();
    if (kthread_should_stop()) return -ECANCELED;
    for (i = 0; i < DESC_COUNT; i++) received += (readl(c->desc + i * 16) >> 12) & 4095;
    if (received != FRAME_BYTES || (status & (BIT(3) | BIT(7) | BIT(8)))) {
        dev_warn_ratelimited(c->dev, "capture incomplete: %u/%u bytes, DMA raw %#x\n", received, FRAME_BYTES, status);
        return -EIO;
    }
    return 0;
}
static int capture_thread(void *data)
{
    struct korvo_cam *c = data;
    while (!kthread_should_stop()) {
        struct capture_buffer *buf;
        int ret;
        wait_event_interruptible(c->wait, kthread_should_stop() || !list_empty(&c->pending));
        if (kthread_should_stop()) break;
        spin_lock_irq(&c->pending_lock);
        buf = list_first_entry(&c->pending, struct capture_buffer, node);
        list_del(&buf->node);
        spin_unlock_irq(&c->pending_lock);
        ret = capture_one(c);
        if (!ret) {
            unsigned int i, offset = 0;
            for (i = 0; i < DESC_COUNT; i++) {
                unsigned int size = min(FRAME_BYTES - offset, DESC_BYTES);
                memcpy(vb2_plane_vaddr(&buf->vb.vb2_buf, 0) + offset, c->frame[i], size);
                offset += size;
            }
            vb2_set_plane_payload(&buf->vb.vb2_buf, 0, FRAME_BYTES);
        }
        buf->vb.sequence = c->sequence++;
        buf->vb.field = V4L2_FIELD_NONE;
        buf->vb.vb2_buf.timestamp = ktime_get_ns();
        vb2_buffer_done(&buf->vb.vb2_buf, ret ? VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);
    }
    return 0;
}
static void return_buffers(struct korvo_cam *c, enum vb2_buffer_state state)
{
    struct capture_buffer *b, *next;
    spin_lock_irq(&c->pending_lock);
    list_for_each_entry_safe(b, next, &c->pending, node) {
        list_del(&b->node);
        vb2_buffer_done(&b->vb.vb2_buf, state);
    }
    spin_unlock_irq(&c->pending_lock);
}
static int queue_setup(struct vb2_queue *q, unsigned int *count, unsigned int *planes,
                       unsigned int sizes[], struct device *alloc_devs[])
{
    if (*planes) return sizes[0] < FRAME_BYTES ? -EINVAL : 0;
    *count = clamp(*count, 2U, 4U);
    *planes = 1;
    sizes[0] = FRAME_BYTES;
    return 0;
}
static int buffer_prepare(struct vb2_buffer *vb)
{
    return vb2_plane_size(vb, 0) < FRAME_BYTES ? -EINVAL : 0;
}
static void buffer_queue(struct vb2_buffer *vb)
{
    struct korvo_cam *c = vb2_get_drv_priv(vb->vb2_queue);
    struct capture_buffer *b = container_of(to_vb2_v4l2_buffer(vb), struct capture_buffer, vb);
    spin_lock_irq(&c->pending_lock);
    list_add_tail(&b->node, &c->pending);
    spin_unlock_irq(&c->pending_lock);
    wake_up(&c->wait);
}
static int stream_start(struct vb2_queue *q, unsigned int count)
{
    struct korvo_cam *c = vb2_get_drv_priv(q);
    int ret = sensor_setup(c);
    if (ret) { return_buffers(c, VB2_BUF_STATE_QUEUED); return ret; }
    c->sequence = 0;
    c->thread = kthread_run(capture_thread, c, "korvo-camera");
    if (IS_ERR(c->thread)) {
        ret = PTR_ERR(c->thread); c->thread = NULL;
        if (!c->sc101iot) sensor_write(c, 0x3008, 0x42);
        return_buffers(c, VB2_BUF_STATE_QUEUED);
        return ret;
    }
    return 0;
}
static void stream_stop(struct vb2_queue *q)
{
    struct korvo_cam *c = vb2_get_drv_priv(q);
    if (c->thread) { kthread_stop(c->thread); c->thread = NULL; }
    dma_stop(c);
    if (!c->sc101iot) sensor_write(c, 0x3008, 0x42);
    return_buffers(c, VB2_BUF_STATE_ERROR);
}
static const struct vb2_ops queue_ops = {
    .queue_setup = queue_setup, .buf_prepare = buffer_prepare, .buf_queue = buffer_queue,
    .start_streaming = stream_start, .stop_streaming = stream_stop,
};
static int querycap(struct file *f, void *p, struct v4l2_capability *cap)
{
    strscpy(cap->driver, "korvo-camera", sizeof(cap->driver));
    struct korvo_cam *c = video_drvdata(f);
    strscpy(cap->card, c->video.name, sizeof(cap->card));
    strscpy(cap->bus_info, "platform:korvo-camera", sizeof(cap->bus_info));
    return 0;
}
static int enum_fmt(struct file *f, void *p, struct v4l2_fmtdesc *fmt)
{
    if (fmt->index) return -EINVAL;
    struct korvo_cam *c = video_drvdata(f);
    fmt->pixelformat = c->sc101iot ? V4L2_PIX_FMT_YUYV : V4L2_PIX_FMT_RGB565X;
    return 0;
}
static int get_fmt(struct file *f, void *p, struct v4l2_format *fmt)
{
    struct korvo_cam *c = video_drvdata(f);
    fmt->fmt.pix = (struct v4l2_pix_format) {
        .width = WIDTH, .height = HEIGHT, .pixelformat = c->sc101iot ? V4L2_PIX_FMT_YUYV : V4L2_PIX_FMT_RGB565X,
        .field = V4L2_FIELD_NONE, .bytesperline = WIDTH * 2,
        .sizeimage = FRAME_BYTES, .colorspace = V4L2_COLORSPACE_SRGB,
    };
    return 0;
}
static int set_fmt(struct file *f, void *p, struct v4l2_format *fmt)
{
    struct korvo_cam *c = video_drvdata(f);
    if (vb2_is_busy(&c->queue)) return -EBUSY;
    return get_fmt(f, p, fmt);
}
static int enum_size(struct file *f, void *p, struct v4l2_frmsizeenum *s)
{
    struct korvo_cam *c = video_drvdata(f);
    if (s->index || s->pixel_format != (c->sc101iot ? V4L2_PIX_FMT_YUYV : V4L2_PIX_FMT_RGB565X)) return -EINVAL;
    s->type = V4L2_FRMSIZE_TYPE_DISCRETE;
    s->discrete.width = WIDTH; s->discrete.height = HEIGHT;
    return 0;
}
static int enum_input(struct file *f, void *p, struct v4l2_input *in)
{
    if (in->index) return -EINVAL;
    struct korvo_cam *c = video_drvdata(f);
    strscpy(in->name, c->sc101iot ? "SC101IOT DVP" : "OV3660 DVP", sizeof(in->name));
    in->type = V4L2_INPUT_TYPE_CAMERA;
    return 0;
}
static int get_input(struct file *f, void *p, unsigned int *i) { *i = 0; return 0; }
static int set_input(struct file *f, void *p, unsigned int i) { return i ? -EINVAL : 0; }
static const struct v4l2_ioctl_ops ioctls = {
    .vidioc_querycap = querycap, .vidioc_enum_fmt_vid_cap = enum_fmt,
    .vidioc_g_fmt_vid_cap = get_fmt, .vidioc_try_fmt_vid_cap = get_fmt,
    .vidioc_s_fmt_vid_cap = set_fmt, .vidioc_enum_framesizes = enum_size,
    .vidioc_enum_input = enum_input, .vidioc_g_input = get_input, .vidioc_s_input = set_input,
    .vidioc_reqbufs = vb2_ioctl_reqbufs, .vidioc_create_bufs = vb2_ioctl_create_bufs,
    .vidioc_querybuf = vb2_ioctl_querybuf, .vidioc_qbuf = vb2_ioctl_qbuf,
    .vidioc_dqbuf = vb2_ioctl_dqbuf, .vidioc_streamon = vb2_ioctl_streamon,
    .vidioc_streamoff = vb2_ioctl_streamoff, .vidioc_expbuf = vb2_ioctl_expbuf,
};
static const struct v4l2_file_operations fops = {
    .owner = THIS_MODULE, .open = v4l2_fh_open, .release = vb2_fop_release,
    .read = vb2_fop_read, .poll = vb2_fop_poll, .mmap = vb2_fop_mmap,
    .unlocked_ioctl = video_ioctl2,
};
static void sensor_release(void *data) { i2c_unregister_device(data); }
static int camera_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct device_node *bus;
    struct resource desc_mem;
    struct i2c_adapter *adapter;
    struct korvo_cam *c;
    struct vb2_queue *q;
    int ret, high, low, i;
    c = devm_kzalloc(dev, sizeof(*c), GFP_KERNEL);
    if (!c) return -ENOMEM;
    c->dev = dev;
    c->cam = devm_platform_ioremap_resource(pdev, 0);
    c->rx = devm_platform_ioremap_resource(pdev, 1);
    c->clock = devm_platform_ioremap_resource(pdev, 2);
    if (IS_ERR(c->cam)) return PTR_ERR(c->cam);
    if (IS_ERR(c->rx)) return PTR_ERR(c->rx);
    if (IS_ERR(c->clock)) return PTR_ERR(c->clock);
    /* XTAL 40 MHz / 2 = XCLK 20 MHz. Only camera-specific clock register. */
    writel(BIT(2) | (1 << 3), c->clock);
    msleep(30);
    bus = of_parse_phandle(dev->of_node, "i2c-bus", 0);
    if (!bus) return -EINVAL;
    adapter = of_find_i2c_adapter_by_node(bus);
    of_node_put(bus);
    if (!adapter) return -EPROBE_DEFER;
    c->sensor = i2c_new_dummy_device(adapter, 0x3c);
    i2c_put_adapter(adapter);
    if (IS_ERR(c->sensor)) return PTR_ERR(c->sensor);
    high = sensor_read(c, 0x300a); low = sensor_read(c, 0x300b);
    if (high != 0x36 || low != 0x60) {
        i2c_unregister_device(c->sensor);
        bus = of_parse_phandle(dev->of_node, "i2c-bus", 0);
        adapter = of_find_i2c_adapter_by_node(bus);
        of_node_put(bus);
        if (!adapter) return -EPROBE_DEFER;
        c->sensor = i2c_new_dummy_device(adapter, 0x68);
        i2c_put_adapter(adapter);
        if (IS_ERR(c->sensor)) return PTR_ERR(c->sensor);
        high = sensor_read8(c, 0xf7); low = sensor_read8(c, 0xf8);
        if (high != 0xda || low != 0x4a) {
            i2c_unregister_device(c->sensor);
            return dev_err_probe(dev, -ENODEV, "No supported sensor detected; SC101IOT ID %x %x\n", high, low);
        }
        c->sc101iot = true;
        /* SC101IOT datasheet table is stable at 8 MHz, YUYV output. */
        writel(BIT(2) | (4 << 3), c->clock);
        msleep(30);
    }
    ret = devm_add_action_or_reset(dev, sensor_release, c->sensor);
    if (ret) return ret;
    ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
    if (ret) return ret;
    bus = of_parse_phandle(dev->of_node, "memory-region", 0);
    if (bus) {
        ret = of_address_to_resource(bus, 0, &desc_mem);
        of_node_put(bus);
        if (ret) return ret;
        if (resource_size(&desc_mem) < DESC_COUNT * 16) return -EINVAL;
        c->desc_dma = desc_mem.start;
    } else {
        /* Compatibility with the first deployed SD-root board DTB. */
        c->desc_dma = 0x2f079c00;
    }
    c->desc = devm_ioremap(dev, c->desc_dma, DESC_COUNT * 16);
    if (!c->desc) return -ENOMEM;
    for (i = 0; i < DESC_COUNT; i++) {
        c->frame[i] = dmam_alloc_coherent(dev, PAGE_SIZE, &c->frame_dma[i], GFP_KERNEL);
        if (!c->frame[i]) return -ENOMEM;
    }
    mutex_init(&c->lock); spin_lock_init(&c->pending_lock);
    INIT_LIST_HEAD(&c->pending); init_waitqueue_head(&c->wait);
    ret = v4l2_device_register(dev, &c->v4l2);
    if (ret) return ret;
    q = &c->queue;
    q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    q->io_modes = VB2_MMAP | VB2_READ;
    q->drv_priv = c; q->buf_struct_size = sizeof(struct capture_buffer);
    q->ops = &queue_ops; q->mem_ops = &vb2_vmalloc_memops;
    q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
    q->lock = &c->lock; q->dev = dev;
    ret = vb2_queue_init(q);
    if (ret) goto err_v4l2;
    strscpy(c->video.name, c->sc101iot ? "Korvo-1 SC101IOT" : "Korvo-1 OV3660", sizeof(c->video.name));
    c->video.v4l2_dev = &c->v4l2; c->video.fops = &fops;
    c->video.ioctl_ops = &ioctls; c->video.lock = &c->lock;
    c->video.queue = q; c->video.release = video_device_release_empty;
    c->video.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
    video_set_drvdata(&c->video, c);
    ret = video_register_device(&c->video, VFL_TYPE_VIDEO, -1);
    if (ret) goto err_v4l2;
    platform_set_drvdata(pdev, c);
    /* This board's dedicated RX channel stays reserved until reboot. */
    __module_get(THIS_MODULE);
    dev_info(dev, "%s detected, registered %s, 240x240, AXI RX1\n", c->video.name, video_device_node_name(&c->video));
    return 0;
err_v4l2:
    v4l2_device_unregister(&c->v4l2);
    return ret;
}
static const struct of_device_id camera_match[] = {
    {.compatible = "espressif,korvo1-ov3660"}, {}
};
MODULE_DEVICE_TABLE(of, camera_match);
/* Keep the board module resident while any video node can be open. */
static struct platform_driver camera_driver = {
    .probe = camera_probe,
    .driver = {.name = "korvo-camera", .of_match_table = camera_match,
               .suppress_bind_attrs = true},
};
module_platform_driver(camera_driver);
MODULE_DESCRIPTION("Korvo-1 OV3660 LCD_CAM V4L2 capture");
MODULE_LICENSE("GPL");
