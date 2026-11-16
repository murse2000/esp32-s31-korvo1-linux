# ESP32-S31 Korvo-1 Linux BSP

A reproducible Buildroot-based Linux image for the ESP32-S31 Korvo-1 board, including the Korvo-1 NOR boot layout and integrated ESP-Hosted Wi-Fi/Bluetooth radio firmware setup.

> This repository includes source snapshots from Espressif Buildroot/BSP projects and their original license files. See [UPSTREAM.md](UPSTREAM.md) before redistributing modified components.

## What is included

- `bsp/` — Espressif ESP32-S31 BSP source snapshot.
- `buildroot/` — Buildroot source snapshot.
- `esp-buildroot-external/` — Espressif Buildroot external tree, including the Korvo-1 defconfig, device tree, Linux fragment, ESP-Hosted package and rootfs setup.
- `korvo1-nor-reference.patch` — patch for the pinned `annoyedmilk/esp32-s31-linux` reference tree.
- `buildroot-host-util-linux-fix.patch` — host util-linux build fix used by the reference tree.
- `build-korvo-wsl.sh` — WSL2 build and 16 MiB image assembly script.
- `flash-korvo1.ps1` — Windows flashing script (default port `COM3`, 921600 baud).
- `build-output/` — final 16 MiB flash image, kernel, SquashFS rootfs, partition table, and explicitly marked pre-Korvo historical image.
- `schematics/` — Korvo-1 schematic PDF.
- `uart_driver/` — CP210x Windows driver package and its vendor license/readme files.

## Build on WSL2 (Ubuntu 24.04)

1. Install WSL2 and Ubuntu 24.04, then open the Ubuntu shell.
2. From the Windows checkout mounted in WSL, run:

   ```bash
   cd /mnt/c/Projects/esp-linux/public-release
   chmod +x build-korvo-wsl.sh
   ./build-korvo-wsl.sh
   ```

   Or pass the checkout path explicitly: `./build-korvo-wsl.sh /mnt/c/path/to/public-release`.
3. The script installs the required Ubuntu packages, fetches pinned ESP-IDF/reference sources, applies the included Korvo-1 and Buildroot patches, builds, then writes images into `build-output/`.

Buildroot and ESP-IDF sources/toolchains are downloaded by the build, so the first run needs network access and substantial disk space. For faster compilation, place the checkout on the WSL Linux filesystem and pass its `/home/...` path.

## Flash

Connect the board's programming UART, identify its Windows COM port, then run in PowerShell:

```powershell
.\flash-korvo1.ps1 -Port COM3
```

The script writes the complete padded 16 MiB image at offset `0x0` using esptool at 921600 baud. Change `COM3` to the port shown by Device Manager. Confirm the board is in its download/flash mode before writing.

## Flash layout

| Name | Offset | Size |
|---|---:|---:|
| NVS | `0x009000` | `0x006000` |
| PHY init | `0x00F000` | `0x001000` |
| factory (Hart 0 firmware) | `0x020000` | `0x1A0000` |
| OpenSBI | `0x1C0000` | `0x030000` |
| Linux (Hart 1) | `0x1F0000` | `0x4C0000` |
| rootfs | `0x6B0000` | `0x7F0000` |
| data/UBI | `0xEA0000` | `0x160000` |

This layout fits a 16 MiB NOR flash. `build-output/korvo1-flash.bin` is padded to exactly 16,777,216 bytes.

## Hardware bring-up recorded

- Hart 0 ESP-Hosted radio firmware and Hart 1 OpenSBI/Linux booted on the Korvo-1.
- Linux mounted the SquashFS root and initialized the UBIFS data overlay.
- `wlan0` and Bluetooth `hci0` registered. Bluetooth is owned by the on-demand `ble-ha` service; Wi-Fi association requires user-provided credentials.
- 800×480 framebuffer and Goodix GT1151 touch were detected; USB HID keyboard was detected by the USB host.
- Camera and audio were not enabled: the referenced BSP did not provide the required Korvo-1 drivers in this build.
- The tested rootfs image is 8,163,328 bytes in a 8,323,072-byte partition. `tmux` was measured and omitted to preserve flash margin.

See [docs/bring-up.md](docs/bring-up.md) and [build-output/SHA256SUMS](build-output/SHA256SUMS).
