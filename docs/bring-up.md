# Korvo-1 bring-up record

Board: ESP32-S31-Korvo-1, 16 MiB NOR flash and 8 GB microSD. On 2026-11-19, the board was updated over UART on COM3 at 921600 baud using `flash-korvo1.ps1`. The current SD-root partition table is written at `0x8000`, the Linux kernel at `0x1F0000`, and its manifest at `0xE9F000`. The manifest offset is calculated from the partition CSV instead of being hardcoded. The writable NOR `data` partition and SD card are preserved.

## Observed boot and devices

- Hart 0 started the ESP-Hosted radio firmware (NG-1.0.6.0.24); Hart 1 booted OpenSBI and Linux 7.3.4.
- The current boot command line uses `/dev/mmcblk0p2` as an ext4 root filesystem. The first microSD partition is mounted at `/mnt/sd`; the live check showed `/dev/root` with approximately 488 MiB and the FAT boot partition with 64 MiB. `/proc/mtd` is not present in this SD-root boot configuration.
- The kernel registered `wlan0` and Bluetooth `hci0`. The on-demand `ble-ha` service manages the Bluetooth radio. A saved WPA2 profile on the microSD root reconnected and received DHCP address 192.168.1.37 after both reboot and a complete power disconnect. The prior SD image had no network block, which explained the failed automatic association. Credentials were supplied locally and are excluded from release sources/images.
- The native `esp32s31-rgb` framebuffer driver programs RGB565 800×480 clocks, porch/sync timings and an AXI TX0 descriptor ring. Blank/unblank and 16/18MHz PCLK changes were exercised. Goodix GT1151 touch and USB HID keyboard were detected. The board has no software dimmable backlight, as confirmed by the official BSP.
- The board audio path was researched against the [official Korvo-1 BSP pin map](https://github.com/espressif/esp-bsp/blob/master/bsp/esp32_s31_korvo_1/API.md), [ESP-IDF S31 GDMA mapping](https://github.com/espressif/esp-idf/blob/master/components/esp_hal_dma/esp32s31/gdma_periph.c), and the [Korvo-1 user guide](https://documentation.espressif.com/esp-dev-kits/en/latest/esp32s31/esp32-s31-korvo-1/index.html). The schematic's 8-bit codec address 0x20 corresponds to Linux 7-bit I²C address 0x10. I²S uses GPIO3 BCLK, GPIO4 WS, GPIO5 DOUT and GPIO6 DIN; the board does not connect codec MCLK. The speaker path includes the ES8389 and NS4150B amplifier.
- The Linux overlay adds an ESP32-S31 AHB-GDMA provider and I²S ASoC CPU-DAI, then links them to the in-tree ES8389 codec driver with a `simple-audio-card`. The Device Tree maps AHB-PDMA input/output sources 36–45 to distinct CLIC slots 17–26. The GDMA mapping follows Espressif's official ESP-IDF table.
- The running kernel registered ALSA card 0 as `Korvo-1 ES8389`, with playback and capture PCM nodes. `aplay -l` reported the I²S/ES8389 device. A 44.1 kHz WAV test completed through `aplay -D hw:0,0`; kernel logs showed an AHB-GDMA cyclic TX channel and I²S playback start/stop. The ES8389 clock-coefficient warning seen in the first run was fixed by encoding `everest,mclk-src` as an 8-bit DT property.
- This verifies ALSA device registration and PCM-to-DMA/I²S transmission. Speaker audibility/level and microphone capture have not been physically confirmed. `mpg123` is installed in the Buildroot SD image and its version was checked, but MP3 file playback has not yet been exercised.

## Camera and GUI bring-up

- The camera actually installed on this board is **SC101IOT**, not the reference board's OV3660. I²C address `0x68`, registers `0xF7/0xF8`, returned ID `0xDA4A`. The [Espressif sensor list](https://github.com/espressif/esp32-camera/blob/master/driver/include/sensor.h) and [SC101IOT driver](https://github.com/espressif/esp32-camera/blob/master/sensors/sc101iot.c) identify this combination.
- `korvo_camera.ko` detects SC101IOT or OV3660 and exposes `/dev/video0`. SC101IOT uses the Espressif initialization table, 8MHz XCLK, YUYV data, and a central 240×240 window. OV3660 setup remains available but has not been tested on a board carrying that sensor.
- A 128KiB contiguous coherent allocation failed after the small PSRAM pool became fragmented. Capture storage was changed to separate coherent pages. AXI descriptor reads from the initial allocation failed with descriptor-error status, so the descriptor list was moved to reserved HP SRAM at `0x2F079C00`. The preceding `0x2F079000..0x2F079BFF` range remains dedicated to the LCD ring. AXI RX1 is camera-only; LCD uses TX0.
- Inverting VSYNC to match Espressif's DVP input routing stabilized frames. A 240×240 PPM photo was captured, opened on the host, and confirmed to contain a real camera image. A subsequent 8-second preview captured 49 complete frames. An initial partial frame was discarded; incomplete frames are never presented as successful images.
- On the final flashed kernel, the LVGL launcher opened the camera screen, displayed live frames, saved a 172,815-byte PPM through its SAVE PHOTO button, and returned HOME. Input events were injected through the real evdev device for this automated UI check. GUI startup switches the framebuffer console into graphics mode to prevent console text from overwriting the interface.
- All eleven official LVGL 9.2.2 demos were enabled and built, including ThorVG vector rendering and the multilingual image-font dependency. The twelve runnable variants (two vector modes) each completed a bounded execution on the board with exit code 0. The touch launcher adds separate audio and camera screens.
- A 32MiB ext4 swap file supplements the existing 6MiB zram swap for GUI workloads. This avoids depending on large free RAM pools and does not imply PC-like rendering performance.

Commands, the complete demo catalog, and implementation paths are documented in [the Korean peripheral guide](korvo-peripherals.ko.md).

### LCD color override correction

The user reported that `lcdctl color red` did not change the physical panel. Reading back the requested sysfs value had only verified command delivery. A live hardware probe then found LCD_START clear, stationary DMA descriptors, and no recurring VSYNC. RGB blank phases (`LCD_BK_EN`, LCD_MISC bit 26) were missing: auto-next-frame alone did not sustain scanout. Enabling the blank phases immediately produced recurring VSYNC and advancing DMA descriptors. The corrected driver enables these phases and redirects the normal AXI DMA descriptor ring to a repeated RGB565 tile in the unused 4KiB at `0x50FFC000` within reserved framebuffer memory. `color off` restores framebuffer addresses without overwriting GUI pixels. Blank also sends a black DMA tile for two frame periods before stopping scanout. The read-only `scanout` attribute reads descriptor/tile memory, and `refresh_active` reads the LCD engine START bit.

On-board checks read the expected DMA tile colors for red, green, blue, white and black; `color off` restored framebuffer descriptors. With the GUI briefly paused to exclude its own drawing, SHA-256 of `/dev/fb0` was identical before and after the color sequence. Blank selected a black tile and cleared LCD_START; unblank plus 16/18MHz clock changes preserved the selected red tile and continuous refresh. These checks verify the DMA source and framebuffer preservation; physical panel appearance requires a visual check separately.

The user visually confirmed that the entire physical panel turned red after the live RGB blank-phase correction. The same correction was then incorporated into the persistent kernel image.

The color-override correction was flashed as kernel build `#3` (2026-11-19 09:21 KST). Without any live register correction, VSYNC repeatedly reasserted after acknowledgment and DMA descriptor addresses advanced. A bounded LVGL camera screen displayed 22 frames while native LCD scanout was active. Wi-Fi reconnected automatically and the board was returned to the demo menu.

### LVGL switching and response correction

The user subsequently reported a black panel on demo selection while the GUI process and framebuffer image remained valid. The fbdev backend requests UNBLANK for each newly executed demo, and the native driver unnecessarily restarted an already active pipeline. UNBLANK is now idempotent; the RGB transfer buffer is enabled and DMA FIFO readiness is checked before starting output. The user confirmed that menus and demos appeared after kernel build `#4` (2026-11-19 09:45 KST). A lower-resolution rendering option was declined, so output and logical UI resolution remain 800×480.

Native rendering was tuned with a 32KiB partial draw buffer (previously 96KiB), style/shadow/header caches, and 10ms input polling. With identical instrumentation excluding empty refresh requests, a 20-frame music-demo startup sample changed from 2,880ms work / 1,029ms flush / 4,822ms wall time to 2,359ms work / 781ms flush / 4,244ms wall time. The 18% reduction in measured work is specific to that sample, not a universal FPS claim.

The LVGL 9.2.2 evdev backend drained multiple queued pointer reports and could collapse a short press/release into RELEASED. A package patch preserves SYN_REPORT boundaries through `continue_reading` and accepts nonnegative multitouch tracking IDs. The on-board regression pauses the GUI, queues a complete short touch, resumes it, and checks widgets entry plus HOME return. Demo/player exec transitions also close inherited descriptors; checks observed a stable 8 descriptors per GUI process.

Audio HOME invokes the launcher with a bare `argv[0]`. Reusing that string as an `execl` filename made the next selection fail with ENOENT and exit. The launcher now resolves `/proc/self/exe` before switching demos. The regression also covers audio → HOME → camera → HOME.

All twelve official variants were exercised through menu selection and HOME, including stress after the queued-input fix. The final on-board regression passed queued widgets/HOME and audio/HOME/camera/HOME; the camera also logged its first displayed frame. The installed GUI/audio binary hashes match the final Buildroot target, and the board was left at the menu with native framebuffer scanout active.

### PPA / DMA2D and direct rendering (2026-11-19)

Kernel `#6` boots with a cached 768KiB canvas reservation and separate camera/PPA SRAM descriptor windows. The new `esp32s31_ppa` misc driver programs PPA Fill/Blend and 2D-DMA rectangle copy/flush without resetting LCD AXI-GDMA, camera AXI RX1, or audio AHB-GDMA. The LVGL launcher and audio player share the new port and automatically show the enabled performance monitor.

Hardware tests passed cropped/strided copy with preserved surrounding pixels, red/green/blue/white/black fills, RGB565 fixed alpha, ARGB8888 pixel alpha, and invalid bounds rejection. A deterministic LVGL scene compared all 384,000 pixels with the software reference. Opaque/rounded/gradient/text/clipped-image output matched exactly; 6,464 alpha pixels differed within one RGB565 step per channel. The test disables the console cursor and performance overlay to avoid unrelated changes in the capture.

With the same monitor-enabled binary, the first 20 music-demo updates measured software 3,930ms work/892ms flush, accelerated partial 2,529ms/163ms, and accelerated DIRECT 1,454ms/166ms. DIRECT executed 78 fills, 78 flushes and 81 blends with zero hardware errors over that run. These are scene-specific measurements, not a promised FPS for all effects. The public [graphics guide](graphics-acceleration.ko.md) records reproduction commands and unsupported operations.

After deployment, the queued-touch integration passed widgets/HOME and audio/HOME/camera/HOME. The camera saved one valid 240×240 photo while native LCD scanout remained active; ES8389 remained registered and Wi-Fi reconnected after the kernel flash. The benchmark started with the performance monitor enabled. Physical panel appearance and perceived smoothness still require the user's direct observation.

Raw clock registers decoded to PCLK 17,999,004Hz, Htotal 928, Vtotal 548, negative pixel edge. This matches the official BSP's 18MHz timing and implies approximately 35.39Hz scanout. No LCD overclock was applied.

### Audio verification commands

Run on the board's Linux shell:

```sh
cat /proc/asound/cards
aplay -l
aplay -D hw:0,0 /mnt/sd/test.wav
mpg123 -a hw:0,0 /mnt/sd/test.mp3
```

Replace `/mnt/sd` with the card's actual mount point. Playback reaching the DMA driver does not by itself prove that an audible speaker signal is present; verify that separately with a speaker/headphone test.

## Storage sizing

The current Linux root filesystem is on the 8 GB microSD card image `korvo1-sdcard.img.xz`. NOR partition CSV assigns the Linux image region `0xCB0000` bytes from `0x1F0000` and writable data `0x160000` bytes from `0xEA0000`. Its last 4096-byte sector at `0xE9F000` holds the kernel-size manifest. The earlier NOR-root layout ended the Linux partition at `0x6B0000`; this layout must not be combined with the current SD-root kernel. `flash-korvo1.ps1` updates the partition table, kernel and manifest together. The full `korvo1-flash.bin` recovery image overwrites all NOR contents. `tmux` was measured at approximately 8.93MiB and omitted from the root filesystem.

## Image integrity

SHA-256 hashes for the published artifacts are in `build-output/SHA256SUMS`. The historical image `s31_full_flash-obsolete-before-korvo1.bin` is retained only as an archive; do not flash it for the current Korvo-1 configuration.
