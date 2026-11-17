# Korvo-1 bring-up record

Board: ESP32-S31-Korvo-1, 16 MiB NOR flash. On 2026-11-17, the board was updated over UART on COM3 at 921600 baud using the sparse flashing script. The partition table, Linux image, kernel-size manifest and rootfs were each written and hash-verified; the writable `data` partition was preserved.

## Observed boot and devices

- Hart 0 started the ESP-Hosted radio firmware (NG-1.0.6.0.24); Hart 1 booted OpenSBI and Linux 7.3.4.
- The fixed MTD map reported `rootfs` as 0x7D0000 bytes and `data` as 0x160000 bytes. SquashFS mounted read-only at `/rom`; the UBIFS volume mounted at `/overlay`, and the overlay root reached the Buildroot shell. `df -h` showed the writable overlay mounted at `/`.
- The kernel registered `wlan0` and Bluetooth `hci0`. The on-demand `ble-ha` service manages the Bluetooth radio. No Wi-Fi credentials were configured, so association was not verified.
- The 800×480 framebuffer, Goodix GT1151 touch controller and USB HID keyboard were detected.
- The ES8389 codec driver is built in and the I²C device is declared at address 0x10, but Linux reported `No soundcards found`. The pinned BSP has no ESP32-S31 I²S CPU-DAI/sound-card driver yet, so audio playback/capture is not available. Camera support was not brought up.

## Storage sizing

The rootfs image is 8,163,328 bytes; its NOR partition is 0x7D0000 (8,192,000 bytes), leaving 28,672 bytes. `tmux` was tested at approximately 8.93 MiB and omitted because it exceeded the rootfs budget. The 0x160000 data partition remains at physical offset 0xEA0000 for writable UBI/UBIFS storage and existing Wi-Fi settings.

## Image integrity

SHA-256 hashes for the published artifacts are in `build-output/SHA256SUMS`. The historical image `s31_full_flash-obsolete-before-korvo1.bin` is retained only as an archive; do not flash it for the current Korvo-1 configuration.
