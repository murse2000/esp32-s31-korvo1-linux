# ESP32-S31 Korvo-1 Linux BSP

[한국어](#한국어) · [English](#english)

---

## 한국어

### 프로젝트 소개

이 저장소는 **ESP32-S31-Korvo-1 보드의 16MiB NOR Flash에서 부팅하는 Buildroot 기반 Linux 이미지**와 그 이미지를 재현하기 위한 소스·패치·스크립트·산출물을 모아 둔 프로젝트입니다. 공식 upstream에 Korvo-1용 NOR defconfig가 없어, Korvo-1 보드 설정과 파티션 배치를 추가하고 ESP32-S31 Linux 레퍼런스의 Hart-0 무선 펌웨어 구성을 결합했습니다.

실제 보드에서 확인한 구성은 Hart 0에서 ESP-Hosted radio firmware를 시작하고 Hart 1에서 OpenSBI와 Linux를 부팅하는 형태입니다. 무선 장치가 정상적으로 등록되면 Linux에서 `wlan0` 및 Bluetooth `hci0`를 볼 수 있습니다. 이 저장소는 사용자가 Wi-Fi에 연결할 수 있도록 `iw`와 `wpa_supplicant`가 포함된 이미지를 제공합니다. Wi-Fi 자격 증명은 이미지나 저장소에 넣지 않습니다.

> 이 저장소는 upstream 프로젝트의 소스 스냅샷도 포함합니다. 각 구성 요소는 원래의 라이선스를 따릅니다. 재배포 전에 [UPSTREAM.md](UPSTREAM.md)와 각 디렉터리의 라이선스·고지 파일을 확인해 주세요.

### 저장소 구성

| 경로 | 내용 |
|---|---|
| `bsp/` | Espressif ESP32-S31 BSP 소스 스냅샷 및 원본 라이선스 |
| `buildroot/` | Buildroot 소스 스냅샷 및 원본 라이선스 |
| `esp-buildroot-external/` | Korvo-1 NOR defconfig, 디바이스 트리, Linux 설정 조각, rootfs overlay, ESP-Hosted 관련 Buildroot 패키지 |
| `korvo1-nor-reference.patch` | 고정된 `annoyedmilk/esp32-s31-linux` 레퍼런스 트리에 Korvo-1 설정과 NOR 파티션 구성을 적용하는 패치 |
| `buildroot-host-util-linux-fix.patch` | 레퍼런스 Buildroot의 host util-linux 빌드 문제를 우회하는 패치 |
| `build-korvo-wsl.sh` | WSL Ubuntu에서 필요한 도구를 준비하고 전체 빌드 및 이미지 조립을 수행하는 스크립트 |
| `flash-korvo1.ps1` | Windows에서 esptool로 보드에 이미지를 기록하는 PowerShell 스크립트 |
| `build-output/` | 현재 Korvo-1 전체 이미지, Linux 커널, SquashFS rootfs, 파티션표, SHA-256 목록 및 이전 이미지 보관본 |
| `docs/bring-up.md` | 실제 보드에서 확인한 부팅 및 주변 장치 기록 |
| `docs/environment/` | 사용한 WSL Ubuntu 24.04 배포판 파일의 검증용 SHA-256 |
| `schematics/` | ESP32-S31 Korvo-1 회로도 PDF |
| `uart_driver/` | Silicon Labs CP210x Windows UART 드라이버와 제조사 라이선스·안내 파일 |
| `UPSTREAM.md` | upstream 저장소 주소, 기준 커밋 및 라이선스 안내 |

중첩 `.git` 디렉터리와 upstream의 전체 Git 이력은 복사하지 않았습니다. 대신 `UPSTREAM.md`에 사용한 각 기준 커밋을 기록했습니다. 빌드 캐시와 임시 로그는 산출물에 포함하지 않습니다. 현재 빌드 스크립트는 포함된 소스 디렉터리를 직접 빌드하는 대신, 고정된 `annoyedmilk/esp32-s31-linux` 커밋과 하위 모듈을 WSL 홈 디렉터리에 받아 패치를 적용해 빌드합니다. 포함된 `bsp/`, `buildroot/`, `esp-buildroot-external/`은 수정·검토 가능한 소스 스냅샷입니다.

### 준비물

- Windows 10/11에서 실행하는 **WSL 2 + Ubuntu 24.04**
- 빌드 첫 실행에 필요한 인터넷 연결: ESP-IDF, toolchain 및 Buildroot 소스/패키지를 내려받습니다.
- 다운로드와 컴파일을 위한 여유 디스크 공간. 빌드 캐시가 커질 수 있으므로 수십 GiB 이상 확보하는 것을 권장합니다.
- 기본 빌드 병렬도는 8개 작업입니다. 메모리가 부족하면 `BR_JOBS`를 낮추고, 빌드를 오래 기다릴 수 있으면 높일 수 있습니다.
- 보드에 이미지를 기록할 경우 UART 연결, Windows COM 포트, Python 3.12 및 Python 환경에서 사용할 수 있는 esptool

Windows의 `/mnt/c` 경로에서 빌드하면 파일 I/O가 느릴 수 있습니다. 더 빠른 빌드를 원하면 저장소를 WSL Linux 파일 시스템(예: `~/projects/esp32-s31-korvo1-linux`)에 복제한 다음 그 경로에서 스크립트를 실행하세요. 어느 위치에서 빌드하든 스크립트는 결과 이미지를 저장소의 `build-output/`에 복사합니다.

### WSL에서 빌드

저장소가 Windows의 `C:\Projects\esp-linux\public-release`에 있는 경우 Ubuntu 터미널에서 다음을 실행합니다.

```bash
cd /mnt/c/Projects/esp-linux/public-release
chmod +x build-korvo-wsl.sh
./build-korvo-wsl.sh
```

다른 경로에 복제했다면 그 경로로 `cd`하면 됩니다. 저장소 경로를 명시하려면 다음과 같이 실행할 수도 있습니다.

```bash
./build-korvo-wsl.sh /mnt/c/path/to/esp32-s31-korvo1-linux
```

스크립트는 다음 작업을 수행합니다.

1. Ubuntu에서 `gcc-14`와 `g++-14`를 설치합니다. 일반 WSL 사용자로 실행하면 이 패키지 설치에 `sudo`를 사용합니다.
2. `annoyedmilk/esp32-s31-linux`의 고정 커밋 `e27b61feb92bc0a6d734c8e5cdb0b6af421c9817`과 하위 모듈을 가져옵니다.
3. `korvo1-nor-reference.patch`와 `buildroot-host-util-linux-fix.patch`를 적용합니다.
4. ESP-IDF 설정 스크립트를 실행하고 ESP32-S31용 외부 toolchain 및 빌드 의존 항목을 준비합니다.
5. Korvo-1 Buildroot defconfig로 OpenSBI, Linux, rootfs와 보드 이미지를 빌드합니다.
6. esptool의 `merge-bin`으로 주소 0부터 시작하는 **16MiB 크기의 패딩된 raw flash 이미지**를 만들고, 커널·rootfs·파티션표를 `build-output/`에 복사합니다.

병렬 작업 수를 지정하는 예:

```bash
BR_JOBS=4 ./build-korvo-wsl.sh
```

빌드는 `${HOME}/esp32-s31-linux-reference` 아래에 소스와 캐시를 유지하며, ESP-IDF는 `${HOME}/esp/esp-idf`를 사용합니다. 기존 경로에 부분 설치 또는 변경된 소스가 있다면 빌드 전에 해당 상태를 확인하세요. 스크립트는 레퍼런스 저장소가 이미 준비되어 있으면 모든 내용을 초기화하지 않습니다.

### 생성되는 이미지

빌드 후 `build-output/`에 다음 파일이 생성되거나 갱신됩니다.

- `korvo1-flash.bin` — 주소 0부터 기록하는 16,777,216바이트 전체 flash 이미지
- `korvo1-linux-Image` — ESP32-S31용 Linux 커널 이미지
- `korvo1-rootfs.squashfs` — 읽기 전용 SquashFS root filesystem
- `korvo1-partitions.csv` — ESP32 파티션 테이블
- `SHA256SUMS` — 게시된 모든 `build-output` 파일의 SHA-256 목록
- `s31_full_flash-obsolete-before-korvo1.bin` — 과거 참고용 이미지. 현재 Korvo-1 빌드에 사용하면 안 됩니다.

저장소에 포함된 이미지의 체크섬을 확인하려면:

```bash
cd build-output
sha256sum -c SHA256SUMS
```

### 16MiB NOR Flash 파티션

| 파티션 | 시작 주소 | 크기 | 용도 |
|---|---:|---:|---|
| `nvs` | `0x009000` | `0x006000` | ESP-IDF NVS 데이터 |
| `phy_init` | `0x00F000` | `0x001000` | RF PHY 초기화 데이터 |
| `factory` | `0x020000` | `0x1A0000` | Hart 0용 factory/radio firmware 이미지 영역 |
| `opensbi` | `0x1C0000` | `0x030000` | OpenSBI |
| `linux` | `0x1F0000` | `0x4C0000` | Hart 1 Linux 이미지 |
| `rootfs` | `0x6B0000` | `0x7F0000` | 읽기 전용 rootfs |
| `data` | `0xEA0000` | `0x160000` | UBI/UBIFS 쓰기 가능 데이터 영역 |

rootfs 이미지는 8,163,328바이트이고 파티션은 8,323,072바이트라서 약 159,744바이트가 남습니다. `tmux`는 약 8.93MiB를 차지하는 것으로 측정되어 rootfs 공간을 초과하므로 최종 구성에서 제외했습니다.

### 보드 플래시

1. 보드의 UART 프로그래밍 포트를 PC에 연결합니다.
2. Windows 장치 관리자에서 COM 포트를 확인합니다. 기존 보드 기록은 `COM3`입니다.
3. 보드를 다운로드/플래시 모드로 둡니다.
4. 저장소 루트의 PowerShell에서 실행합니다.

```powershell
.\flash-korvo1.ps1 -Port COM3
```

기본 포트는 `COM3`, 속도는 921600 baud이며, 전체 16MiB 이미지가 주소 `0x0`에 기록됩니다. 포트가 다르면 `COM7` 등 실제 번호로 바꿉니다. 스크립트는 Windows Python launcher를 통해 Python 3.12의 `esptool` 모듈을 실행하므로 해당 환경에 esptool이 설치되어 있어야 합니다.

Ubuntu 안에서 직접 기록하려면 빌드 스크립트가 생성한 Python 환경에서 `esptool`을 사용하고 `FLASH_PORT`를 설정할 수 있습니다.

```bash
FLASH_PORT=/dev/ttyUSB0 ./build-korvo-wsl.sh
```

USB-UART 포트 이름은 환경에 따라 달라집니다. WSL에서 USB 장치를 전달하는 별도 설정이 없으면 Windows PowerShell의 플래시 스크립트를 사용하는 방법이 간단합니다.

### 보드에서 Wi-Fi 검색 및 접속

현재 이미지는 `iw`와 `wpa_supplicant`를 포함합니다. 보드 콘솔은 root shell로 열립니다. 아래 명령은 **보드에서**, Linux 콘솔에 입력합니다. WSL이나 Windows 터미널에서 실행하는 명령이 아닙니다.

#### 1. 인터페이스와 무선 도구 확인

```sh
ip link show wlan0
iw dev
```

`wlan0`가 보이지 않는다면 먼저 부팅 로그와 모듈 상태를 확인합니다.

```sh
lsmod
dmesg | grep -iE 'esp|hosted|wlan|firmware|80211'
```

#### 2. 주변 SSID 검색

```sh
ip link set wlan0 up
iw dev wlan0 scan | grep -E '^(BSS|[[:space:]]*SSID:|[[:space:]]*signal:)'
```

출력에서 `SSID:` 뒤에 표시되는 이름이 네트워크 이름입니다. 보이지 않는 네트워크이거나 검색 권한/지역 설정 때문에 검색 결과가 없을 수도 있습니다. AP가 5GHz 전용인지, 해당 지역의 채널인지도 확인하세요.

#### 3. WPA/WPA2 개인 네트워크 접속

보드의 임시 파일 시스템(`/tmp`)에 설정 파일을 만듭니다. 아래의 `내_WiFi_이름`과 `내_WiFi_비밀번호`를 실제 값으로 바꾸세요. 따옴표 안에 큰따옴표나 역슬래시가 있는 SSID/암호는 아래 예제를 그대로 사용할 수 없으므로 별도 이스케이프 처리가 필요합니다.

```sh
umask 077
cat > /tmp/wpa_supplicant.conf <<'EOF'
ctrl_interface=/var/run/wpa_supplicant
update_config=0
country=KR
network={
    ssid="내_WiFi_이름"
    psk="내_WiFi_비밀번호"
    key_mgmt=WPA-PSK
}
EOF
chmod 600 /tmp/wpa_supplicant.conf
```

설정 파일을 지정해 접속을 시작하고 DHCP로 IP 주소를 받습니다.

```sh
wpa_supplicant -B -P /run/wpa_supplicant.wlan0.pid -i wlan0 -c /tmp/wpa_supplicant.conf
udhcpc -i wlan0
```

`country=KR`은 규제 도메인을 대한민국으로 지정합니다. 보드가 다른 국가에서 작동한다면 실제 사용 국가에 맞게 바꿔야 합니다. 공개 이슈, 로그, 저장소 등에 Wi-Fi 암호를 올리지 마세요. 이 설정은 `/tmp`에만 있으므로 재부팅하면 사라집니다.

#### 4. 연결 상태 확인

```sh
iw dev wlan0 link
ip addr show dev wlan0
ping -c 3 1.1.1.1
ping -c 3 example.com
```

`iw ... link`에서 AP에 연결되었다고 표시되고 `ip addr`에 IPv4 주소가 있으면 무선 연결과 DHCP가 완료된 것입니다. IP 주소 ping은 되지만 도메인 ping이 실패하면 DNS 설정/임대 정보를 확인합니다.

#### 5. 연결 종료 또는 재접속

```sh
kill "$(cat /run/wpa_supplicant.wlan0.pid)"
```

다시 연결할 때는 `wpa_supplicant`와 `udhcpc` 명령을 다시 실행합니다. 현재 root filesystem은 읽기 전용이고 `/tmp`, `/run`은 메모리 파일시스템입니다. 따라서 위 설정은 재부팅 후 유지되지 않습니다. 자동 연결을 원하면 rootfs overlay에 적절한 시작 스크립트와 안전한 자격 증명 저장 방법을 추가한 다음 이미지를 다시 빌드해야 합니다. 암호를 공개 저장소에 커밋하지 마세요.

#### 문제 해결

- `wlan0`가 없음: `dmesg`에서 ESP-Hosted/radio firmware와 WLAN 드라이버 오류를 확인하고 Hart 0 radio firmware가 시작됐는지 살펴봅니다.
- 검색 결과가 없음: `ip link set wlan0 up`을 했는지 확인하고 AP가 2.4GHz를 제공하는지, SSID를 숨기지 않았는지 확인합니다.
- 인증 실패: SSID와 암호의 대소문자, WPA 설정을 확인합니다. 기업용 802.1X 네트워크는 이 간단한 PSK 예제와 다르게 설정해야 합니다.
- 연결은 됐지만 IP가 없음: `udhcpc -i wlan0`을 다시 실행하고 DHCP 서버/AP 상태를 확인합니다.
- DHCP IP는 있지만 DNS가 안 됨: 임대된 resolver 설정을 살펴봅니다.

### 보드에서 확인한 항목과 미지원 항목

실제 Korvo-1 보드에서 확인한 사항과 현재 이미지의 범위는 [docs/bring-up.md](docs/bring-up.md)에 기록했습니다. 핵심 결과는 다음과 같습니다.

- COM3 UART, 921600 baud로 이미지를 기록하고 부팅 확인
- Hart 0 ESP-Hosted radio firmware NG-1.0.6.0.24, Hart 1 OpenSBI 및 Linux 7.3.4 부팅
- SquashFS rootfs 마운트 및 UBI/UBIFS 데이터 overlay 초기화
- Linux에서 `wlan0` 및 Bluetooth `hci0` 등록. Wi-Fi AP 연결과 성능은 이 기록에서 검증하지 않았으며, 사용자가 SSID와 암호를 제공해 직접 연결해야 합니다.
- 800×480 framebuffer, Goodix GT1151 터치, USB HID 키보드 감지
- 카메라 및 오디오 드라이버는 현재 빌드에 포함되지 않음

이 목록은 모든 주변 장치가 완전히 동작한다는 보증이 아닙니다. 검증한 구성요소와 아직 확인하지 않은 기능을 구분해 기록합니다.

### 재현 기준과 라이선스

사용한 기준 커밋, 고정 레퍼런스 커밋, 구성요소별 라이선스 안내는 [UPSTREAM.md](UPSTREAM.md)를 확인하세요. 빌드 산출물의 SHA-256은 [build-output/SHA256SUMS](build-output/SHA256SUMS)에 있습니다. upstream의 라이선스와 저작권 고지는 각 디렉터리에 보존했습니다.

---

## English

### Project overview

This repository collects a **Buildroot-based Linux image for the ESP32-S31-Korvo-1 board with a 16 MiB NOR flash layout**, together with the source snapshots, patches, scripts, and generated images. The upstream BSP did not provide a Korvo-1 NOR defconfig, so this project adds the board configuration and partition layout and applies the Hart-0 radio setup from the ESP32-S31 Linux reference project. The current build script downloads the pinned `annoyedmilk/esp32-s31-linux` tree and its submodules into the WSL home directory, applies the included patches, and builds from that checkout. The `bsp/`, `buildroot/`, and `esp-buildroot-external/` directories are source snapshots included for inspection and further development; the script does not build directly from those in-repository copies.

On the tested board, the ESP-Hosted radio firmware starts on Hart 0 while OpenSBI and Linux boot on Hart 1. When the radio and Linux WLAN driver initialize successfully, Linux exposes `wlan0` and Bluetooth `hci0`. The image includes `iw` and `wpa_supplicant` for manual Wi-Fi setup. Wi-Fi credentials are deliberately not stored in the repository or firmware image.

> Included upstream source snapshots retain their original licensing. Review [UPSTREAM.md](UPSTREAM.md) and the license/notice files in each component before redistribution.

### Repository contents

| Path | Contents |
|---|---|
| `bsp/` | Espressif ESP32-S31 BSP source snapshot and upstream license |
| `buildroot/` | Buildroot source snapshot and upstream licensing files |
| `esp-buildroot-external/` | Korvo-1 NOR defconfig, device tree, Linux fragment, rootfs overlay, and ESP-Hosted-related Buildroot package |
| `korvo1-nor-reference.patch` | Korvo-1 configuration and NOR layout patch for the pinned reference tree |
| `buildroot-host-util-linux-fix.patch` | Host util-linux build workaround for that reference build |
| `build-korvo-wsl.sh` | WSL build, dependency setup, and image assembly script |
| `flash-korvo1.ps1` | Windows PowerShell flashing script |
| `build-output/` | Current flash image, kernel, SquashFS rootfs, partition table, checksums, and a clearly marked historical image |
| `docs/bring-up.md` | Hardware boot and peripheral bring-up record |
| `docs/environment/` | SHA-256 reference for the WSL Ubuntu 24.04 distribution archive used during setup |
| `schematics/` | ESP32-S31 Korvo-1 schematic PDF |
| `uart_driver/` | Silicon Labs CP210x Windows UART driver and vendor notices |
| `UPSTREAM.md` | Upstream repositories, pinned revisions, and licensing notes |

Nested `.git` directories and full upstream Git histories are not included. The exact source snapshots are identified by commit in `UPSTREAM.md`. Build caches and temporary local logs are omitted.

### Requirements

- **WSL 2 with Ubuntu 24.04** on Windows 10/11
- Internet access for the initial download of ESP-IDF, toolchains, Buildroot source, and packages
- Tens of GiB of free disk space for downloads and build output
- Python 3.12 with the `esptool` module available when flashing from Windows
- UART connection and the board's Windows COM port

The default build parallelism is eight jobs. Reduce it with `BR_JOBS` if memory is limited. Building from `/mnt/c` can be slower because of cross-filesystem I/O; cloning into the WSL Linux filesystem is recommended for faster builds.

### Build under WSL

For a Windows checkout at `C:\Projects\esp-linux\public-release`, run in Ubuntu:

```bash
cd /mnt/c/Projects/esp-linux/public-release
chmod +x build-korvo-wsl.sh
./build-korvo-wsl.sh
```

For a different checkout location, change the `cd` path or pass the repository path explicitly:

```bash
./build-korvo-wsl.sh /mnt/c/path/to/esp32-s31-korvo1-linux
```

The script installs `gcc-14` and `g++-14` (using `sudo` for package installation when run as a regular user), checks out reference commit `e27b61feb92bc0a6d734c8e5cdb0b6af421c9817` and its submodules, applies the two patches, prepares ESP-IDF/toolchains, builds OpenSBI, Linux and the root filesystem, then assembles a 16 MiB raw image. Generated files are copied to the repository's `build-output/` directory.

To set build parallelism:

```bash
BR_JOBS=4 ./build-korvo-wsl.sh
```

The script keeps its working reference checkout under `${HOME}/esp32-s31-linux-reference`, ESP-IDF under `${HOME}/esp/esp-idf`, and build/download caches below the reference checkout. It reuses existing directories; inspect any existing checkout before rebuilding if you have changed it locally.

### Build artifacts and integrity

- `korvo1-flash.bin` — complete 16,777,216-byte image, padded for writing at flash offset `0x0`
- `korvo1-linux-Image` — Linux kernel image
- `korvo1-rootfs.squashfs` — read-only root filesystem
- `korvo1-partitions.csv` — ESP partition table
- `SHA256SUMS` — checksums for every file in `build-output/`
- `s31_full_flash-obsolete-before-korvo1.bin` — historical reference only; **do not flash it for the current Korvo-1 setup**

Verify the published artifacts with:

```bash
cd build-output
sha256sum -c SHA256SUMS
```

### 16 MiB NOR flash map

| Partition | Offset | Size | Purpose |
|---|---:|---:|---|
| `nvs` | `0x009000` | `0x006000` | ESP-IDF NVS data |
| `phy_init` | `0x00F000` | `0x001000` | RF PHY initialization data |
| `factory` | `0x020000` | `0x1A0000` | Hart-0 factory/radio firmware image region |
| `opensbi` | `0x1C0000` | `0x030000` | OpenSBI |
| `linux` | `0x1F0000` | `0x4C0000` | Hart-1 Linux image |
| `rootfs` | `0x6B0000` | `0x7F0000` | Read-only root filesystem |
| `data` | `0xEA0000` | `0x160000` | Writable UBI/UBIFS data area |

The SquashFS image is 8,163,328 bytes in an 8,323,072-byte rootfs partition, leaving 159,744 bytes free. `tmux` was measured at about 8.93 MiB and omitted because it would exceed the remaining rootfs space.

### Flash the board

1. Connect the board's programming UART to the PC.
2. Find its COM port in Windows Device Manager. The recorded setup used `COM3`.
3. Put the board into download/flash mode.
4. From PowerShell at the repository root, run:

```powershell
.\flash-korvo1.ps1 -Port COM3
```

The script defaults to `COM3`, uses 921600 baud, and writes the full image at offset `0x0`. Substitute the actual port if it differs. The Windows Python launcher must provide Python 3.12 with `esptool` installed.

The build script can also invoke esptool in its ESP-IDF Python environment when `FLASH_PORT` is set, for example `FLASH_PORT=/dev/ttyUSB0 ./build-korvo-wsl.sh`. USB device forwarding into WSL is environment-dependent; using the Windows PowerShell script is often simpler.

### Search for Wi-Fi networks and connect from `wlan0`

The image includes `iw` and `wpa_supplicant`. Run these commands **on the board's Linux console**, which opens a root shell; do not run them in Windows or the WSL build shell.

Check the interface and tools:

```sh
ip link show wlan0
iw dev
```

If `wlan0` is missing, inspect modules and kernel messages:

```sh
lsmod
dmesg | grep -iE 'esp|hosted|wlan|firmware|80211'
```

Bring the interface up and scan nearby access points:

```sh
ip link set wlan0 up
iw dev wlan0 scan | grep -E '^(BSS|[[:space:]]*SSID:|[[:space:]]*signal:)'
```

The SSID appears after `SSID:`. An empty result can mean the interface is down, the network is hidden, or the AP's band/channel is unavailable to the current radio configuration.

For a WPA/WPA2 personal network, create a temporary configuration on the board. Replace the sample network name and password. This simple quoted format does not cover SSIDs/passwords containing quotes or backslashes.

```sh
umask 077
cat > /tmp/wpa_supplicant.conf <<'EOF'
ctrl_interface=/var/run/wpa_supplicant
update_config=0
country=KR
network={
    ssid="My-WiFi-SSID"
    psk="My-WiFi-Password"
    key_mgmt=WPA-PSK
}
EOF
chmod 600 /tmp/wpa_supplicant.conf
```

Start WPA authentication and request an IP address using DHCP:

```sh
wpa_supplicant -B -P /run/wpa_supplicant.wlan0.pid -i wlan0 -c /tmp/wpa_supplicant.conf
udhcpc -i wlan0
```

`country=KR` sets the regulatory country to South Korea; change it to the country where the device is actually operating. Do not publish Wi-Fi credentials in logs, issues, or this repository. The config is stored in `/tmp` and disappears after reboot.

Check the association, assigned address, IP connectivity, and DNS:

```sh
iw dev wlan0 link
ip addr show dev wlan0
ping -c 3 1.1.1.1
ping -c 3 example.com
```

To stop the supplicant:

```sh
kill "$(cat /run/wpa_supplicant.wlan0.pid)"
```

Repeat the supplicant and DHCP commands after reboot. The current root filesystem is read-only and `/tmp` and `/run` are volatile. Persistent automatic connection requires adding a startup script and a safe credential-storage approach to the rootfs overlay, then rebuilding. Do not commit a real password.

Troubleshooting:

- **No `wlan0`:** inspect kernel messages for ESP-Hosted/radio firmware and WLAN driver errors; confirm Hart-0 radio firmware started.
- **No scan results:** bring the link up first, check that the AP provides 2.4 GHz, and verify whether its SSID is hidden.
- **Authentication fails:** check SSID/password capitalization and security mode. Enterprise 802.1X networks require a different configuration.
- **Associated but no IP:** rerun `udhcpc -i wlan0` and check the AP's DHCP service.
- **IP ping works but DNS fails:** inspect the resolver information received from DHCP.

### Hardware validation and limitations

See [docs/bring-up.md](docs/bring-up.md) for the detailed record. The tested board booted the Hart-0 ESP-Hosted radio firmware NG-1.0.6.0.24 and Hart-1 OpenSBI/Linux 7.3.4, mounted SquashFS, initialized UBI/UBIFS, and detected the 800×480 framebuffer, Goodix GT1151 touch, and USB HID keyboard. `wlan0` and Bluetooth `hci0` registered; association with a Wi-Fi access point was not part of the recorded validation. Camera and audio drivers were not included in this build.

These are observations from the tested configuration, not a guarantee that every peripheral or feature is fully supported.

### Revisions and licenses

See [UPSTREAM.md](UPSTREAM.md) for source repositories, exact snapshot revisions, and licensing notes. Artifact checksums are in [build-output/SHA256SUMS](build-output/SHA256SUMS). Upstream copyright and license notices remain with their source components.
