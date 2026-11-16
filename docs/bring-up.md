# Korvo-1 bring-up record

Board: ESP32-S31-Korvo-1, 16 MiB NOR flash. The board was flashed over UART on COM3 at 921600 baud with the image in `build-output/korvo1-flash.bin`.

## Observed boot and devices

- Hart 0 started the ESP-Hosted radio firmware (NG-1.0.6.0.24); Hart 1 booted OpenSBI and Linux 7.3.4.
- SquashFS mounted read-only. The data partition initialized UBIFS and the writable overlay, with a 22-PEB / 18-LEB maximum reported at boot.
- The kernel registered `wlan0` and Bluetooth `hci0`. The on-demand `ble-ha` service manages the Bluetooth radio. No Wi-Fi credentials were configured, so association was not verified.
- The 800×480 framebuffer, Goodix GT1151 touch controller and USB HID keyboard were detected.
- Camera and audio were not brought up; the pinned reference BSP lacked the required drivers/configuration.

## Storage sizing

The rootfs image is 8,163,328 bytes; its NOR partition is 0x7F0000 (8,323,072 bytes), leaving 159,744 bytes. `tmux` was tested at approximately 8.93 MiB and omitted because it exceeded the remaining rootfs budget. The current image reserves the 0x160000 data partition for writable UBI/UBIFS storage.

## Image integrity

SHA-256 hashes for the published artifacts are in `build-output/SHA256SUMS`. The historical image `s31_full_flash-obsolete-before-korvo1.bin` is retained only as an archive; do not flash it for the current Korvo-1 configuration.
