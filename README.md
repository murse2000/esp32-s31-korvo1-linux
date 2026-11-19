# ESP32-S31 Korvo-1 Linux BSP

[한국어](#한국어) · [English](#english)

> 최신 Wi-Fi 자동접속, native LCD 제어, SC101IOT/OV3660 카메라, 전체 LVGL 공식 데모 사용법은 [Korvo-1 주변 장치 안내](docs/korvo-peripherals.ko.md)를 확인하세요.
> LVGL은 800×480 네이티브 해상도를 유지합니다. Linux용 PPA/2D-DMA 드라이버와 LVGL 연동으로 색 채우기·이미지 복사·알파 합성·화면 전송을 가속하며, 공식 BSP의 DIRECT_MODE를 참고한 전체 화면 렌더링 버퍼를 사용합니다. FPS/CPU 모니터도 켰습니다. 구현 범위와 검증 방법은 [그래픽 가속 안내](docs/graphics-acceleration.ko.md)에 정리했습니다.
> 스크롤은 변경되지 않은 픽셀을 2D-DMA로 재사용하고, 작은 곡선·반투명 제어는 A8 합성으로 처리합니다. 인터럽트 대기와 캐시 동기화도 수정했습니다. 같은 드라이버에서 이전 렌더러와 교차 비교한 CPU 시간 중앙값은 스크롤 약 52%, 슬라이더 약 49% 감소했습니다. [조건·원시 수치·화면 비교 결과](tests/results/interaction-2026-11-19.md)를 함께 제공합니다.
> 후속 작업에서는 CPU를 계속 소모하던 offscreen canvas 대기 루프와 작은 변경의 부분 갱신을 수정했습니다. 카메라 버퍼는 필요할 때만 할당하고 데모 음악은 microSD 이미지에 미리 포함합니다. 추가 캐시·묶음 처리는 반복 측정에서 항상 유리하지 않아 별도 `korvo-ui-extra`로 제공합니다. 기본 모드는 기존 2D 가속 그리기를 유지합니다. [측정값·검증·재현 방법](tests/results/cpu-2026-11-19.md)을 확인하세요.

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
| `linux/` | S31 AHB-GDMA와 I²S/ASoC CPU-DAI를 포함한 Linux 소스 오버레이 |
| `korvo-addons/` | Wi-Fi 재연결 서비스, V4L2 카메라 모듈, LVGL 전체 데모와 터치 메뉴, LCD 도구 |
| `korvo1-nor-reference.patch` | 고정된 `annoyedmilk/esp32-s31-linux` 레퍼런스 트리에 Korvo-1 설정과 NOR 파티션 구성을 적용하는 패치 |
| `buildroot-host-util-linux-fix.patch` | 레퍼런스 Buildroot의 host util-linux 빌드 문제를 우회하는 패치 |
| `build-korvo-wsl.sh` | WSL Ubuntu에서 필요한 도구를 준비하고 전체 빌드 및 이미지 조립을 수행하는 스크립트 |
| `flash-korvo1.ps1` | Windows에서 esptool로 보드에 이미지를 기록하는 PowerShell 스크립트 |
| `build-output/` | 현재 Korvo-1 flash/SD 이미지, Linux 커널, 파티션표, SHA-256 목록 및 이전 이미지 보관본 |
| `docs/bring-up.md` | 실제 보드에서 확인한 부팅 및 주변 장치 기록 |
| `docs/environment/` | 사용한 WSL Ubuntu 24.04 배포판 파일의 검증용 SHA-256 |
| `schematics/` | ESP32-S31 Korvo-1 회로도 PDF |
| `uart_driver/` | Silicon Labs CP210x Windows UART 드라이버와 제조사 라이선스·안내 파일 |
| `UPSTREAM.md` | upstream 저장소 주소, 기준 커밋 및 라이선스 안내 |

중첩 `.git` 디렉터리와 upstream의 전체 Git 이력은 복사하지 않았습니다. 대신 `UPSTREAM.md`에 사용한 각 기준 커밋을 기록했습니다. 빌드 캐시와 임시 로그는 산출물에 포함하지 않습니다. 현재 빌드 스크립트는 포함된 소스 디렉터리를 직접 빌드하는 대신, 고정된 `annoyedmilk/esp32-s31-linux` 커밋과 하위 모듈을 WSL 홈 디렉터리에 받아 패치를 적용해 빌드합니다. 포함된 `bsp/`, `buildroot/`, `esp-buildroot-external/`, `linux/`은 수정·검토 가능한 소스입니다.

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

1. Ubuntu에 `gcc-14`와 `g++-14`가 없으면 설치합니다. 일반 WSL 사용자로 실행하면 패키지 설치에 `sudo`를 사용합니다.
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
- `korvo1-sdcard.img.xz` — microSD에서 부팅하는 현재 Linux root filesystem 이미지
- `korvo1-rootfs.squashfs` — 이전 NOR rootfs 배치의 참고 산출물이며 현재 SD 부팅 구성에서는 사용하지 않음
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
| `linux` | `0x1F0000` | `0xCB0000` | Hart 1 kernel 영역 및 manifest. 현재 root filesystem은 microSD에서 부팅 |
| `data` | `0xEA0000` | `0x160000` | UBI/UBIFS 쓰기 가능 데이터 영역 |

현재 Linux root filesystem은 microSD 이미지(`korvo1-sdcard.img.xz`)에서 부팅합니다. 16MiB NOR의 Linux 영역은 `0x1F0000`부터 `0xEA0000`까지이며, `flash-korvo1.ps1`은 파티션표·커널·크기 manifest를 기록하고 UBI 데이터 영역은 덮어쓰지 않습니다. `tmux`는 약 8.93MiB로 측정되어 microSD rootfs 구성에서도 제외했습니다.

### 보드 플래시

1. 보드의 UART 프로그래밍 포트를 PC에 연결합니다.
2. Windows 장치 관리자에서 COM 포트를 확인합니다. 기존 보드 기록은 `COM3`입니다.
3. 보드를 다운로드/플래시 모드로 둡니다.
4. 저장소 루트의 PowerShell에서 실행합니다.

```powershell
.\flash-korvo1.ps1 -Port COM3
```

기본 포트는 `COM3`, 속도는 921600 baud입니다. 스크립트는 파티션표를 `0x8000`, Linux kernel을 `0x1F0000`, kernel-size manifest를 `0xE9F000`에 기록하며 microSD나 UBI 데이터 영역은 건드리지 않습니다. 포트가 다르면 `COM7` 등 실제 번호로 바꿉니다. 스크립트는 Windows Python launcher를 통해 Python 3.12의 `esptool` 모듈을 실행하므로 해당 환경에 esptool이 설치되어 있어야 합니다. `korvo1-flash.bin`은 전체 16MiB 복구 이미지이며 NOR 데이터를 모두 덮어씁니다.

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

#### 3. 여러 Wi-Fi 네트워크 저장 및 자동 접속

이 이미지에는 부팅 시 실행되는 `/etc/init.d/S40wifi`가 포함되어 있습니다. 이 서비스는 `/etc/wpa_supplicant.conf`를 읽어 `wpa_supplicant`를 시작하고 연결을 감시하며 DHCP 주소를 받습니다. 현재 root filesystem은 microSD의 ext4 파티션이고 쓰기 가능하므로 `/etc` 설정은 재부팅 후에도 유지됩니다. 설정을 위해 다시 빌드하거나 플래시할 필요가 없습니다.

기본 설정의 전역 옵션은 보존하고, 아래 `network` 블록을 파일 끝에 추가하세요. 비밀번호는 보드 터미널에서 직접 입력하고 저장소나 채팅에 올리지 마세요.

```sh
cat >> /etc/wpa_supplicant.conf <<'EOF'
network={
    ssid="109-0903_WIFI"
    psk="여기에_비밀번호"
    key_mgmt=WPA-PSK
    priority=10
}
EOF
chmod 600 /etc/wpa_supplicant.conf
```

두 번째 네트워크도 같은 방식으로 블록을 추가합니다.

```sh
cat >> /etc/wpa_supplicant.conf <<'EOF'
network={
    ssid="두번째_SSID"
    psk="두번째_비밀번호"
    key_mgmt=WPA-PSK
    priority=5
}
EOF
chmod 600 /etc/wpa_supplicant.conf
```

`priority` 숫자가 클수록 우선 연결됩니다. 저장된 네트워크가 여러 개면 `wpa_supplicant`가 주기적으로 주변을 검색하고, 사용할 수 있는 네트워크를 선택하며 연결이 끊기면 재접속을 시도합니다. 별도 반복 검색 스크립트는 필요하지 않습니다. 설정을 바로 적용하려면 다음을 실행하세요.

```sh
/etc/init.d/S40wifi restart
```

재부팅 후에는 다음 명령으로 연결과 IP 주소를 확인합니다.

```sh
iw dev wlan0 link
ip addr show dev wlan0
```

앞서 만든 설정이 `/etc/wifi.conf`에만 있다면 자동 접속 서비스가 읽지 않습니다. 해당 파일의 `network` 블록을 `/etc/wpa_supplicant.conf`로 옮기세요. 이미 있는 네트워크 블록은 중복으로 추가하지 마세요. 따옴표나 역슬래시가 포함된 SSID/암호는 별도 이스케이프 처리가 필요합니다.

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

다시 연결할 때는 `/etc/init.d/S40wifi restart`를 실행합니다. 현재 root filesystem은 microSD ext4로 쓰기 가능하며, `/tmp`와 `/run`은 메모리 파일시스템입니다. 설정 파일은 `/etc/wpa_supplicant.conf`에 저장되므로 재부팅 후에도 유지됩니다. 암호를 공개 저장소에 커밋하지 마세요.

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
- microSD의 ext4 root filesystem 부팅 및 FAT boot partition을 `/mnt/sd`에 마운트
- Linux에서 `wlan0` 및 Bluetooth `hci0` 등록. microSD에 저장한 Wi-Fi 프로필로 재부팅과 전체 전원 분리 후 자동접속·DHCP 주소 할당을 확인했습니다. 공개 이미지에는 개인 프로필이 없으므로 `wifi-add 'SSID'`로 처음 한 번 등록해야 합니다.
- 800×480 framebuffer, Goodix GT1151 터치, USB HID 키보드 감지
- LCD는 native `esp32s31-rgb` 드라이버에서 타이밍·DMA·blank/unblank를 관리하며 `lcdctl`로 제어합니다. 실제 장착된 카메라는 SC101IOT(`0x68`, ID `0xDA4A`)로 확인했고 V4L2 사진 저장과 미리보기를 구현했습니다. 공식 LVGL 9.2.2 데모 11종(벡터 2가지 방식 포함 12개 실행 항목)은 터치 메뉴로 선택할 수 있습니다. 자세한 사용법과 검증 범위는 [주변 장치 안내](docs/korvo-peripherals.ko.md)에 있습니다.
- 인터넷에서 확인한 Espressif Korvo-1 보드 핀맵과 ESP-IDF S31 AHB-PDMA IRQ 표를 기준으로 AHB-GDMA, ESP32-S31 I²S CPU-DAI, ES8389 코덱 및 simple-audio-card 연결을 구현했습니다. 커널 로그에서 ES8389 probe와 `Korvo-1 ES8389` 카드 등록을 확인했고, ALSA가 playback/capture PCM 장치를 만들었습니다.
- `aplay -l`에서 카드 0 `Korvo-1 ES8389`를 확인했습니다. 44.1 kHz WAV를 `aplay -D hw:0,0 ...`로 재생할 때 I²S playback 시작/종료 및 AHB-GDMA cyclic TX 전송 로그가 확인됐고, codec clock coefficient 오류는 수정했습니다. 이는 Linux→DMA→I²S PCM 경로의 검증입니다. 실제 스피커에서 들리는 음량·음질은 귀로 확인하지 않았으므로 아날로그 출력까지 검증 완료라고 단정하지 않습니다.
- 현재 Buildroot microSD rootfs에는 `aplay`와 `mpg123`이 포함됩니다. MP3 디코더 버전은 확인했지만 보드에서 MP3 파일 재생 자체는 아직 시험하지 않았습니다.

이 목록은 모든 주변 장치가 완전히 동작한다는 보증이 아닙니다. 검증한 구성요소와 아직 확인하지 않은 기능을 구분해 기록합니다.

### 성능과 지연 진단

"ping을 실행하면 CPU 사용률이 흔들리고 UART 콘솔이 느리다"는 증상을 COM3 UART에서 저부하로 측정해 정리했습니다. USB host IRQ(초당 약 3,500회)와 LVGL idle 루프는 원인이 아니었고, 9 MiB RAM에서 page cache가 부족해 생기는 SD 재읽기와 kswapd 스캔이 원인이었습니다. MMC readahead를 128 KiB에서 8 KiB로 낮추는 `S06readahead` init 스크립트를 추가했습니다. 측정값과 기각한 가설, 되돌린 변경은 [docs/performance-diagnostics.ko.md](docs/performance-diagnostics.ko.md)에 있습니다.

#### ES8389 사운드 재생과 녹음

이 드라이버는 공식 [Korvo-1 BSP 핀맵](https://github.com/espressif/esp-bsp/blob/master/bsp/esp32_s31_korvo_1/API.md) 및 [ESP-IDF S31 GDMA 연결표](https://github.com/espressif/esp-idf/blob/master/components/esp_hal_dma/esp32s31/gdma_periph.c)를 참고해 Linux 7.3.4용으로 추가했습니다. I²S는 GPIO3(BCLK), GPIO4(WS), GPIO5(DOUT), GPIO6(DIN)를 사용하며 보드에서 ES8389로 가는 별도 MCLK 연결은 없습니다. ES8389는 I²C 7-bit 주소 `0x10`이고, 스피커 출력은 코덱 뒤 NS4150B 앰프와 연결됩니다. [보드 사용자 안내서](https://documentation.espressif.com/esp-dev-kits/en/latest/esp32s31/esp32-s31-korvo-1/index.html)는 ES8389와 NS4150B를 이 보드의 오디오 부품으로 명시합니다.

커널 설정에는 S31 AHB-GDMA, I²S ASoC CPU-DAI, ES8389 codec와 simple-audio-card가 포함됩니다. 현재 보드에서 ALSA 카드 0과 playback/capture PCM 장치, GDMA playback 전송을 확인했습니다. WAV 재생은 다음처럼 실행합니다.

```sh
cat /proc/asound/cards
aplay -l
aplay -D hw:0,0 /mnt/sd/your-audio.wav
```

microSD rootfs에는 ALSA `aplay`와 MP3 디코더 `mpg123`을 포함합니다. MP3 파일은 다음처럼 재생할 수 있습니다.

```sh
mpg123 -a hw:0,0 /mnt/sd/your-audio.mp3
```

파일 경로는 실제 microSD 마운트 경로로 바꾸세요. 확인한 것은 ALSA 장치 등록과 Linux PCM 버퍼가 GDMA/I²S로 전달되는 경로입니다. 현재 기록에서는 스피커에서 들리는 실제 음량/음질과 마이크 녹음 결과를 청취·측정하지 않았으므로, 이를 아날로그 입출력까지 완전히 검증한 것으로 해석하면 안 됩니다.

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
| `build-output/` | Current flash and microSD images, kernel, partition table, checksums, and a clearly marked historical image |
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

The script installs `gcc-14` and `g++-14` only when missing (using `sudo` for package installation when run as a regular user), checks out reference commit `e27b61feb92bc0a6d734c8e5cdb0b6af421c9817` and its submodules, applies the two patches, prepares ESP-IDF/toolchains, builds OpenSBI, Linux and the root filesystem, then assembles a 16 MiB raw image. Generated files are copied to the repository's `build-output/` directory.

To set build parallelism:

```bash
BR_JOBS=4 ./build-korvo-wsl.sh
```

The script keeps its working reference checkout under `${HOME}/esp32-s31-linux-reference`, ESP-IDF under `${HOME}/esp/esp-idf`, and build/download caches below the reference checkout. It reuses existing directories; inspect any existing checkout before rebuilding if you have changed it locally.

### Build artifacts and integrity

- `korvo1-flash.bin` — complete 16,777,216-byte image, padded for writing at flash offset `0x0`
- `korvo1-linux-Image` — Linux kernel image
- `korvo1-sdcard.img.xz` — current Linux root filesystem image for the microSD card
- `korvo1-rootfs.squashfs` — retained artifact from the prior NOR-rootfs layout; unused by the current SD-root setup
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
| `linux` | `0x1F0000` | `0xCB0000` | Hart-1 kernel region and size manifest; root filesystem boots from microSD |
| `data` | `0xEA0000` | `0x160000` | Writable UBI/UBIFS data area |

The Linux root filesystem boots from `korvo1-sdcard.img.xz`. The NOR Linux partition spans `0x1F0000` through `0xEA0000`; `flash-korvo1.ps1` writes the partition table at `0x8000`, the kernel at `0x1F0000` and its size manifest at `0xE9F000`, leaving the UBI data region untouched. `korvo1-flash.bin` is a full 16 MiB recovery image and overwrites all NOR contents. `tmux` was measured at about 8.93 MiB and omitted from the root filesystem.

### Flash the board

1. Connect the board's programming UART to the PC.
2. Find its COM port in Windows Device Manager. The recorded setup used `COM3`.
3. Put the board into download/flash mode.
4. From PowerShell at the repository root, run:

```powershell
.\flash-korvo1.ps1 -Port COM3
```

The script defaults to `COM3` and uses 921600 baud. It writes the partition table, kernel and its manifest together, leaving the microSD card and writable NOR data partition untouched. Substitute the actual port if it differs. The Windows Python launcher must provide Python 3.12 with `esptool` installed. `korvo1-flash.bin` remains available for a full-image recovery, which overwrites all 16 MiB including writable data.

The build script can also invoke esptool in its ESP-IDF Python environment when `FLASH_PORT` is set, for example `FLASH_PORT=/dev/ttyUSB0 ./build-korvo-wsl.sh`. USB device forwarding into WSL is environment-dependent; using the Windows PowerShell script is often simpler.

### Search for Wi-Fi networks and connect from `wlan0`

The image includes `iw`, `wpa_supplicant`, and a boot service at `/etc/init.d/S40wifi`. It reads `/etc/wpa_supplicant.conf`, starts the supplicant, monitors the link, and obtains a DHCP lease. The root filesystem is writable ext4 on microSD, so `/etc` changes persist across reboots. No rebuild or reflash is needed. Run these commands **on the board's Linux console**, which opens a root shell.

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

Append a network block to the existing config so its global options stay in place. Replace the password locally on the board; do not publish it.

```sh
cat >> /etc/wpa_supplicant.conf <<'EOF'
network={
    ssid="109-0903_WIFI"
    psk="Replace-With-Your-Password"
    key_mgmt=WPA-PSK
    priority=10
}
EOF
chmod 600 /etc/wpa_supplicant.conf
```

Add one `network` block per saved SSID. Higher `priority` values are preferred. The running `wpa_supplicant` scans periodically, selects an available saved network, and retries after disconnection; no separate scan loop is needed. Apply changes immediately with:

```sh
/etc/init.d/S40wifi restart
```

At boot, `S40wifi` reads the saved config and connects automatically. If the credentials were saved only in `/etc/wifi.conf`, copy the network block into `/etc/wpa_supplicant.conf`; the startup service does not read `/etc/wifi.conf`. Do not append duplicate network blocks. The quoted example needs escaping for SSIDs or passwords containing quotes or backslashes.

Check the association, assigned address, IP connectivity, and DNS:

```sh
iw dev wlan0 link
ip addr show dev wlan0
ping -c 3 1.1.1.1
ping -c 3 example.com
```

After reboot, verify the connection with `iw dev wlan0 link` and `ip addr show dev wlan0`. Do not commit a real password.

Troubleshooting:

- **No `wlan0`:** inspect kernel messages for ESP-Hosted/radio firmware and WLAN driver errors; confirm Hart-0 radio firmware started.
- **No scan results:** bring the link up first, check that the AP provides 2.4 GHz, and verify whether its SSID is hidden.
- **Authentication fails:** check SSID/password capitalization and security mode. Enterprise 802.1X networks require a different configuration.
- **Associated but no IP:** rerun `udhcpc -i wlan0` and check the AP's DHCP service.
- **IP ping works but DNS fails:** inspect the resolver information received from DHCP.

### Hardware validation and limitations

See [docs/bring-up.md](docs/bring-up.md) for the detailed record and [the peripheral guide](docs/korvo-peripherals.ko.md) for commands. The board boots Hart-0 ESP-Hosted radio firmware and Hart-1 OpenSBI/Linux 7.3.4. Wi-Fi reconnects after reboot from a profile on microSD. The native `esp32s31-rgb` framebuffer driver controls LCD timings and DMA; the Goodix touchscreen and USB HID keyboard are supported. The camera fitted to the tested board is SC101IOT (I²C `0x68`, ID `0xDA4A`); the V4L2 driver captures a central 240×240 YUYV window, with RGB conversion for PPM photos and preview. The driver also supports OV3660 detection and setup, but that sensor path was not exercised on this board. All eleven official LVGL 9.2.2 demos are included (twelve entries with both vector rendering variants), together with the Korvo audio and camera screens. ES8389 PCM playback reaches I²S/AHB-GDMA TX; audible speaker output and microphone capture still need physical confirmation.

These are observations from the tested configuration, not a guarantee that every peripheral or feature is fully supported.

#### Performance and latency

The "CPU swings while ping runs, UART console feels slow" report was measured over COM3 UART with no SSH session. About 3,500 USB host IRQs per second and the LVGL idle loop were ruled out. The cause is repeated SD refetching and kswapd scanning driven by a starved page cache on 9 MiB of RAM. `S06readahead` lowers the MMC readahead from 128 KiB to 8 KiB. Numbers, rejected hypotheses and a reverted change are in [docs/performance-diagnostics.ko.md](docs/performance-diagnostics.ko.md) (Korean).

#### ES8389 audio

The Linux 7.3.4 overlay adds the ESP32-S31 AHB-GDMA provider and I²S ASoC CPU-DAI, then links them to the in-tree ES8389 codec driver with `simple-audio-card`. The implementation used the [official Korvo-1 BSP pin map](https://github.com/espressif/esp-bsp/blob/master/bsp/esp32_s31_korvo_1/API.md), [official S31 GDMA signal table](https://github.com/espressif/esp-idf/blob/master/components/esp_hal_dma/esp32s31/gdma_periph.c), and [Korvo-1 hardware guide](https://documentation.espressif.com/esp-dev-kits/en/latest/esp32s31/esp32-s31-korvo-1/index.html). I²S uses GPIO3 BCLK, GPIO4 WS, GPIO5 DOUT, and GPIO6 DIN; the codec MCLK pin is not wired on this board. The ES8389 is at Linux I²C address `0x10` and the speaker path goes through an NS4150B amplifier.

On the tested board, `/proc/asound/cards` and `aplay -l` showed `Korvo-1 ES8389`; playback and capture PCM nodes were created. A WAV played with `aplay -D hw:0,0` completed and kernel logs showed cyclic AHB-GDMA TX and I²S playback start/stop. This confirms the Linux PCM-to-DMA/I²S path, but physical speaker audibility and microphone capture have not been confirmed. The microSD Buildroot image includes `aplay` and `mpg123`; an MP3 decode/play test remains to be performed.

```sh
cat /proc/asound/cards
aplay -l
aplay -D hw:0,0 /mnt/sd/test.wav
mpg123 -a hw:0,0 /mnt/sd/test.mp3
```

Replace `/mnt/sd` with the card's actual mount path. See [docs/bring-up.md](docs/bring-up.md) for board test details.

### Revisions and licenses

See [UPSTREAM.md](UPSTREAM.md) for source repositories, exact snapshot revisions, and licensing notes. Artifact checksums are in [build-output/SHA256SUMS](build-output/SHA256SUMS). Upstream copyright and license notices remain with their source components.
