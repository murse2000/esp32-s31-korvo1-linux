# Korvo-1 bring-up record

Board: ESP32-S31-Korvo-1, 16 MiB NOR flash and 8 GB microSD. On 2026-11-19, the board was updated over UART on COM3 at 921600 baud using `flash-korvo1.ps1`. The Linux kernel and its size manifest were written to `0x1F0000` and `0x6AF000`; esptool verified both against the host files. The writable NOR `data` partition and SD card were preserved during this kernel-only update.

## Observed boot and devices

- Hart 0 started the ESP-Hosted radio firmware (NG-1.0.6.0.24); Hart 1 booted OpenSBI and Linux 7.3.4.
- The current boot command line uses `/dev/mmcblk0p2` as an ext4 root filesystem. The first microSD partition is mounted at `/mnt/sd`; the live check showed `/dev/root` with approximately 488 MiB and the FAT boot partition with 64 MiB. `/proc/mtd` is not present in this SD-root boot configuration.
- The kernel registered `wlan0` and Bluetooth `hci0`. The on-demand `ble-ha` service manages the Bluetooth radio. No Wi-Fi credentials were configured, so association was not verified.
- The 800×480 framebuffer, Goodix GT1151 touch controller and USB HID keyboard were detected.
- The board audio path was researched against the [official Korvo-1 BSP pin map](https://github.com/espressif/esp-bsp/blob/master/bsp/esp32_s31_korvo_1/API.md), [ESP-IDF S31 GDMA mapping](https://github.com/espressif/esp-idf/blob/master/components/esp_hal_dma/esp32s31/gdma_periph.c), and the [Korvo-1 user guide](https://documentation.espressif.com/esp-dev-kits/en/latest/esp32s31/esp32-s31-korvo-1/index.html). The schematic's 8-bit codec address 0x20 corresponds to Linux 7-bit I²C address 0x10. I²S uses GPIO3 BCLK, GPIO4 WS, GPIO5 DOUT and GPIO6 DIN; the board does not connect codec MCLK. The speaker path includes the ES8389 and NS4150B amplifier.
- The Linux overlay adds an ESP32-S31 AHB-GDMA provider and I²S ASoC CPU-DAI, then links them to the in-tree ES8389 codec driver with a `simple-audio-card`. The Device Tree maps AHB-PDMA input/output sources 36–45 to distinct CLIC slots 17–26. The GDMA mapping follows Espressif's official ESP-IDF table.
- The running kernel registered ALSA card 0 as `Korvo-1 ES8389`, with playback and capture PCM nodes. `aplay -l` reported the I²S/ES8389 device. A 44.1 kHz WAV test completed through `aplay -D hw:0,0`; kernel logs showed an AHB-GDMA cyclic TX channel and I²S playback start/stop. The ES8389 clock-coefficient warning seen in the first run was fixed by encoding `everest,mclk-src` as an 8-bit DT property.
- This verifies ALSA device registration and PCM-to-DMA/I²S transmission. Speaker audibility/level and microphone capture have not been physically confirmed. `mpg123` is installed in the Buildroot SD image and its version was checked, but MP3 file playback has not yet been exercised. Camera support was not brought up.

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

The current Linux root filesystem is on the 8 GB microSD card image `korvo1-sdcard.img.xz`. NOR partition CSV assigns the Linux image region `0xCB0000` bytes from `0x1F0000` and writable data `0x160000` bytes from `0xEA0000`. The flashed kernel-size manifest ends the current kernel payload at the `0x6B0000` boundary. `flash-korvo1.ps1` updates only the kernel and manifest; the full `korvo1-flash.bin` recovery image overwrites all NOR contents. `tmux` was measured at approximately 8.93 MiB and omitted from the root filesystem.

## Image integrity

SHA-256 hashes for the published artifacts are in `build-output/SHA256SUMS`. The historical image `s31_full_flash-obsolete-before-korvo1.bin` is retained only as an archive; do not flash it for the current Korvo-1 configuration.
