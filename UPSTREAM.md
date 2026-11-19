# Upstream sources and licensing

The source directories retain their upstream license and notice files. Each component remains under the license supplied with that component. Review those files before redistribution or reuse; this project does not replace upstream licensing.

| Component | Source | Snapshot |
|---|---|---|
| ESP Linux BSP | https://github.com/espressif/esp-linux-bsp | `a427cfbf5cccebd4f4e571db8a111ff1f5d31d71` |
| Buildroot | https://gitlab.com/buildroot.org/buildroot | `c28996768fc69b583ff50132f8c2760abc0fae07` |
| Espressif Buildroot external tree | https://github.com/espressif/esp-buildroot-external | `d75370b7ca068e3c64ecdd81a08f53e51e013db4` plus the Korvo-1 working-tree changes in this repository |
| ESP32-S31 Korvo-1 reference and radio integration | https://github.com/annoyedmilk/esp32-s31-linux | `e27b61feb92bc0a6d734c8e5cdb0b6af421c9817`, with `korvo1-nor-reference.patch` applied |
| CP210x Windows UART driver | Silicon Labs package; see files in `uart_driver/` | Vendor package retained with its license and notices |
| Korvo-1 schematic | `schematics/esp32-s31-korvo-1-schematics.pdf` | Board reference document supplied with the project materials |

## Peripheral driver references

The Korvo-1 audio implementation in `linux/` was developed against these public hardware/software references:

| Reference | Used for |
|---|---|
| [Espressif Korvo-1 BSP API](https://github.com/espressif/esp-bsp/blob/master/bsp/esp32_s31_korvo_1/API.md) | Official board signal and peripheral definitions |
| [ESP-IDF ESP32-S31 GDMA peripheral map](https://github.com/espressif/esp-idf/blob/master/components/esp_hal_dma/esp32s31/gdma_periph.c) | AHB/AXI/LP-AHB DMA input/output interrupt-source mapping |
| [ESP32-S31 Korvo-1 User Guide](https://documentation.espressif.com/esp-dev-kits/en/latest/esp32s31/esp32-s31-korvo-1/index.html) | ES8389 codec and NS4150B speaker amplifier identification |
| [annoyedmilk ESP32-S31 Linux reference](https://github.com/annoyedmilk/esp32-s31-linux) | Existing S31 Linux board and subsystem bring-up context |
| [ESP32-S31 Linux documentation reference](https://github.com/GrieferPig/esp32-s31-linux-docs) | Community Linux hardware and driver notes |

The kernel sources added under `linux/` retain their SPDX identifiers. The AHB-GDMA and I²S CPU-DAI drivers are Linux-side implementations; the official ESP-IDF source is used as a hardware interrupt/signal reference, not copied as Linux driver code. The ES8389 codec driver itself is the Linux kernel's in-tree `sound/soc/codecs/es8389.c` driver.

The camera module in `korvo-addons/package/korvo-camera/` detects the SC101IOT actually fitted to the tested board and the OV3660 described by the reference board. SC101IOT initialization values come from [Espressif esp32-camera](https://github.com/espressif/esp32-camera/blob/master/sensors/private_include/sc101iot_settings.h); OV3660 values come from [Espressif esp-video-components](https://github.com/espressif/esp-video-components/blob/master/esp_cam_sensor/sensors/ov3660/private_include/ov3660_settings.h). Both retained tables carry Apache-2.0 attribution. The native Linux LCD and camera integration drivers use GPL-2.0-only identifiers; project user-space tools use MIT identifiers.

The demo build downloads **LVGL v9.2.2**, MIT licensed, from [the official LVGL repository](https://github.com/lvgl/lvgl/tree/v9.2.2). It enables all eleven official demo families, including the bundled ThorVG vector renderer. Original library licenses and image/font asset attributions remain with that downloaded source. The board launcher and original generated melody are project additions. See [the peripheral guide](docs/korvo-peripherals.ko.md) for exact source locations and commands.

The project-specific patches and scripts were added to make the Korvo-1 16 MiB NOR configuration reproducible. Source snapshots are included so this repository remains self-contained; nested upstream `.git` directories and their histories are not copied. Use the snapshot hashes above when comparing against upstream history.

## Graphics acceleration references

The Linux module in `korvo-addons/package/korvo-ppa/` is GPL-2.0-only; its UAPI, diagnostic tool and LVGL client are MIT licensed. Register programming was developed using the Apache-2.0 [ESP32-S31 PPA HAL](https://github.com/espressif/esp-idf/blob/master/components/esp_hal_ppa/esp32s31/include/hal/ppa_ll.h), [2D-DMA HAL](https://github.com/espressif/esp-idf/blob/master/components/esp_hal_dma/esp32s31/include/hal/dma2d_ll.h), and `components/esp_driver_ppa/src/{ppa_fill,ppa_blend}.c` as hardware references. No ESP-IDF runtime or FreeRTOS object is linked into Linux.

The [official Korvo-1 display implementation](https://github.com/espressif/esp-bsp/blob/master/bsp/esp32_s31_korvo_1/src/bsp_display.c) and [LVGL demo profile](https://github.com/espressif/esp-bsp/blob/master/examples/display_lvgl_demos/sdkconfig.bsp.esp32_s31_korvo_1) informed the native RGB565 display, DMA separation, DIRECT canvas and performance-monitor setup. The Linux implementation uses one cached render canvas and a distinct scanout framebuffer; it does not reproduce the BSP's VSYNC buffer exchange or bounce-buffer ISR. Unsupported draw operations retain LVGL's own software renderer.

The LVGL package retains its MIT license with two small core patches: an optional weak scroll hook (ordinary ports retain full invalidation), and Tabview event registration limited to the two events its existing callback handles. The Linux client uses that hook to reuse validated scroll pixels from the previous scanout frame, with full redraw for unsupported scenes. PPA completion uses Linux IRQ/completion APIs; directional DMA mappings and explicit rectangle cache synchronization follow the Linux DMA API. A8 masks use LVGL's own rounded-rectangle mask implementation. See [the interaction measurements and pixel checks](tests/results/interaction-2026-11-19.md) for validation and limits.
