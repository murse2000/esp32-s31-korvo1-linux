# ESP32-S31 Linux PPA / 2D-DMA와 LVGL

이 구성은 화면 출력과 UI 렌더링을 모두 **800×480 RGB565**로 유지합니다. ESP-IDF 전용 API 대신 Linux 커널 모듈 `esp32s31_ppa.ko`와 LVGL 9.2.2용 draw unit을 구현했습니다. 드라이버 로드만으로 모든 LVGL 명령이 빨라지는 것은 아닙니다. 아래 지원 조건에 맞는 작업만 가속기에 보내며 나머지는 기존 LVGL 소프트웨어 렌더러가 처리합니다.

## 공식 Korvo-1 펌웨어에서 참고한 부분

사용자가 제공한 [공식 Korvo-1 BSP](https://github.com/espressif/esp-bsp/tree/master/bsp/esp32_s31_korvo_1)와 [LVGL 데모 설정](https://github.com/espressif/esp-bsp/blob/master/examples/display_lvgl_demos/sdkconfig.bsp.esp32_s31_korvo_1)을 확인했습니다. 공식 예제는 `DIRECT_MODE`, RGB framebuffer 2개, bounce buffer, performance monitor를 선택합니다. BSP의 기본 시작 함수는 DMA용 내부 메모리 버퍼를 요청합니다. 예제 설정과 BSP 기본값은 구분해야 합니다.

현재 Linux에서는 framebuffer를 직접 CPU로 쓰던 방식에서, **캐시가 가능한 전체 화면 canvas에 그린 뒤 변경 영역만 DMA2D로 LCD framebuffer에 복사**하는 방식으로 변경했습니다. 부분 렌더링에서 같은 이미지/효과의 draw task를 여러 타일마다 반복하는 비용을 줄이기 위한 변경입니다. LCD의 scanout framebuffer와 렌더링 canvas는 서로 분리되어 있습니다. LCD VSYNC에 맞춘 이중 scanout buffer 교환이나 공식 bounce-buffer ISR 자체를 이식한 것은 아니며, 화면 찢김 방지를 보장하지 않습니다.

공식 `esp_lvgl_port`의 PPA 설정은 화면 회전용 옵션입니다. 이 저장소의 채우기·합성 draw unit은 별도로 구현한 Linux 연동이며, 공식 BSP가 모든 UI를 PPA로 렌더링한다고 가정하지 않습니다. 현재 PPA SRM 확대·회전·미러링과 임의 각도 이미지 변환은 이 draw unit의 지원 범위에 포함하지 않습니다.

## 실제 가속 경로

| 작업 | 처리 경로 | 조건 |
|---|---|---|
| 단색 배경 채우기 | PPA Fill | 완전 불투명, gradient 없음, 사각형 또는 둥근 사각형의 모서리가 없는 중앙 영역 |
| 둥근 배경·반투명 배경 | PPA A8 Blend | LVGL의 기존 반지름 마스크를 사용, gradient 없음. 모서리 안티앨리어싱 유지 |
| 넓고 짧은 불투명 제어 | 단순 채우기 + 모서리 SW | 65,536픽셀 이하 중앙부는 빠른 무마스크 CPU 채우기, 모서리는 기존 SW. 반복 DMA 대기보다 저렴한 경로 선택 |
| RGB565 이미지 복사 | 2D-DMA | 원본 크기, 일반 합성, 색상 재지정/마스크 없음 |
| RGB565 고정 투명도 합성 | PPA Blend | 위와 같은 조건, 전역 opacity 적용 |
| ARGB8888 이미지 합성 | PPA Blend | 픽셀 alpha 및 전역 opacity, premultiplied 입력 제외 |
| 변경 영역을 LCD framebuffer로 복사 | 2D-DMA | canvas의 stride와 사각형 좌표를 사용하며 CPU의 비캐시 framebuffer 복사를 대체 |
| 변경되지 않은 스크롤 내용 | 2D-DMA | 이전 scanout에서 canvas로 복사. 새로 드러난 띠·고정 테두리만 다시 그리기 |
| 글자·그림자·gradient·곡선·변환 이미지·복합 마스크·다른 색 형식 | LVGL software | 화면 내용과 효과를 유지 |

작은 작업은 장치 호출 비용을 고려해 512픽셀 이상부터 선택합니다. RGB565 DMA 수신은 32비트 쓰기 정렬이 필요합니다. 홀수 폭/목적지 좌표의 작업도 정렬된 내부는 가속하고, 양쪽 최대 한 열씩만 소프트웨어로 그립니다. 무효화 영역은 가로 2픽셀 경계로 확장합니다. 이는 해상도 축소가 아닙니다. 지원하지 않는 offscreen layer도 소프트웨어로 그립니다.

CPU와 DMA가 함께 사용하는 메모리에는 Linux DMA API로 캐시 clean/invalidate를 수행합니다. RISC-V 플랫폼 장치의 기본 coherent 설정과 `dma_skip_sync`가 실제 하드웨어와 맞지 않아, probe에서 비일관성 DMA 장치로 명시했습니다. DMA가 읽는 source는 `DMA_TO_DEVICE`, 쓰고 읽는 destination은 `DMA_BIDIRECTIONAL`로 각 전송에 맞춰 매핑합니다. 읽기만 한 canvas를 복사 후 무효화하던 처리를 제거했습니다. 폭이 좁고 stride가 긴 사각형은 실제 행만 동기화해 사이에 있는 다른 픽셀의 캐시를 보존합니다.

새 DT에서는 2D-DMA RX0 완료 인터럽트(SoC source 105 → Hart 1 CLIC slot 41)를 사용합니다. 동기 ioctl이 완료를 기다리는 동안 프로세스는 잠들고 다른 작업이나 idle이 실행될 수 있습니다. IRQ가 없는 이전 DT에서는 polling 방식으로 호환 동작합니다. 완료 신호뿐 아니라 실제 픽셀과 주변 영역 보존도 검사합니다.

## 메모리와 다른 장치

| 물리 주소 | 용도 |
|---|---|
| `0x50DB8000`부터 `0xC0000` 바이트 | 캐시 가능한 LVGL DIRECT canvas. 부팅 시 예약해 연속 메모리 단편화 회피 |
| `0x50F40000`부터 `0xC0000` 바이트 | 기존 LCD framebuffer와 단색 검사 타일 |
| `0x2F079000`부터 `0xC00` 바이트 | LCD AXI-GDMA TX0 descriptor |
| `0x2F079C00`부터 `0x200` 바이트 | 카메라 AXI-GDMA RX1 descriptor. 실제 29개×16=464바이트 |
| `0x2F079E00`부터 `0x200` 바이트 | PPA / 2D-DMA descriptor |

PPA와 2D-DMA는 LCD·카메라의 AXI-GDMA 및 오디오의 AHB-GDMA와 별도 블록입니다. 기존 LCD/카메라 전역 DMA reset을 사용하지 않습니다. canvas는 약 768KiB의 일반 RAM을 추가로 예약하므로 복잡한 데모에서는 swap 사용량도 함께 확인해야 합니다. 부트로더의 framebuffer 주소는 유지합니다.

## 보드 사용법과 성능 표시

부팅 서비스 `/etc/init.d/S54ppa`가 모듈을 로드하고, 메뉴 및 `audio` 플레이어가 같은 가속 포트를 사용합니다. 모듈이나 전용 버퍼를 사용할 수 없으면 기존 소프트웨어 framebuffer 포트로 시작합니다. 전송 오류가 발생하면 해당 프로세스는 경고를 기록하고 소프트웨어로 계속 그립니다.

```sh
cat /var/log/ppa.log
ls -l /dev/esp32s31-ppa
ppa-test --stats
cat /var/log/korvo-ui.log
```

`LV_USE_SYSMON=1`, `LV_USE_PERF_MONITOR=1`, `LV_USE_PERF_MONITOR_LOG_MODE=0`으로 빌드했습니다. 화면 오른쪽 아래 FPS와 CPU 표시가 나타납니다. Linux에서는 `LV_SYSMON_GET_IDLE=korvo_linux_cpu_idle`로 `/proc/stat`의 실제 CPU 시간 변화량을 사용합니다. idle과 iowait는 CPU 실행 시간에서 제외합니다. 이전 LVGL timer 기반 CPU 값은 DMA 완료 대기와 페이지 읽기 대기도 busy로 계산했습니다. FPS는 계속 LVGL 갱신 계측값이며, 패널의 물리 주사율과 구분해야 합니다. I/O를 기다리며 느린 경우 CPU 값이 낮게 나올 수 있으므로 CPU 수치만으로 반응 속도를 판단하지 않습니다.

![실제 framebuffer에서 캡처한 DIRECT 메뉴와 오른쪽 아래 성능 표시](images/korvo-lvgl-ppa-menu.png)

`fbset`의 `accel false`는 fbdev의 전통적인 가속 인터페이스 표시입니다. 이 구현은 별도 `/dev/esp32s31-ppa`를 통해 LVGL이 직접 요청하므로, 실제 사용 여부는 `ppa-test --stats`로 확인합니다.

LCD 클럭은 공식 BSP와 대조하고 하드웨어 레지스터를 읽어 다시 확인했습니다. `HP_SYS_CLKRST.LCDCAM_LCD_CTRL0=0x0716483D`, `LCD_CLOCK=0x80000141`이며, PLL160M에서 `N=8`, `B=201`, `A=226`, 픽셀 prescale=1로 나누어 **17,999,004Hz**입니다. 공식 18MHz에서 약 0.006% 이내 차이입니다. H total=928, V total=548이고 계산 주사율은 약 35.39Hz입니다. H pulse/back/front=40/40/48, V pulse/back/front=23/32/13, PCLK 하강 에지도 [공식 정의](https://github.com/espressif/esp-bsp/blob/master/bsp/esp32_s31_korvo_1/include/bsp/esp32_s31_korvo_1.h)와 일치합니다. 이 레지스터 검사는 별도 계측기로 GPIO 클럭을 측정한 결과는 아닙니다.

비교 실행은 GUI 서비스를 멈춘 뒤 하나씩 수행합니다. 테스트 중에는 화면을 터치하지 않습니다.

```sh
/etc/init.d/S97lvgl-music-demo stop
# 기존 CPU 렌더링 + CPU framebuffer 복사
KORVO_PPA_DISABLE=1 KORVO_UI_STATS=1 korvo-ui music 15
# PPA + DMA2D, 작은 타일을 쓰는 비교 경로
KORVO_PPA_PARTIAL=1 KORVO_UI_STATS=1 korvo-ui music 15
# 기본 전체 화면 DIRECT canvas 경로
KORVO_UI_STATS=1 korvo-ui music 15
ppa-test --stats
/etc/init.d/S97lvgl-music-demo start
```

`KORVO_UI_STATS`는 실제 flush가 발생한 갱신 20회를 집계하며 종료할 때 가속 명령 수와 오류 수를 출력합니다. 어떤 화면에서 실제로 가속했는지는 `copy/fill/flush/blend` 증가로 확인할 수 있습니다. 특정 장면에서 fill이 0일 수 있으며, 확대·변환된 이미지가 많은 구간은 여전히 CPU의 영향을 받습니다.

2026-11-19 보드 커널 `#6`에서 FPS 모니터를 켠 동일 실행 파일로 위 세 명령을 순서대로 실행한 결과입니다. 각 모드의 **첫 실제 갱신 20회**를 비교했습니다.

| 경로 | 구간 시간(대기 포함) | 처리 시간 | framebuffer flush 시간 |
|---|---:|---:|---:|
| Software | 6,282ms | 3,930ms | 892ms |
| PPA + DMA2D / partial | 4,281ms | 2,529ms | 163ms |
| PPA + DMA2D / DIRECT | 3,234ms | 1,454ms | 166ms |

이 구간의 DIRECT 처리 시간은 software 대비 약 **63%**, partial 가속 대비 약 **43%** 감소했습니다. flush 시간은 software 대비 약 **81%** 감소했습니다. 구간 시간 전체가 처리 시간과 같지는 않습니다. 데모의 타이머, 메모리/cache 상태, swap, FPS 오버레이의 갱신도 결과에 영향을 줍니다. 이는 시작 구간 한 번의 비교이며 모든 데모의 고정 배속이나 60FPS를 뜻하지 않습니다. 기본 메뉴의 성능 오버레이 캡처에서는 25FPS가 표시됐지만, 정적 메뉴의 갱신 빈도로 음악 애니메이션의 FPS를 추정하면 안 됩니다.

DIRECT 음악 실행 동안 실제 하드웨어 명령은 `fill=78`, `flush=78`, `blend=81`, `errors=0`이었습니다. 따라서 화면 복사뿐 아니라 합성 경로도 사용됐음을 확인했습니다.

## 스크롤과 슬라이더 CPU 부하 수정

커널 `#7`부터 RX0 완료 인터럽트와 방향에 맞는 캐시 동기화를 사용합니다. 작은 둥근 제어·투명 배경은 A8 합성을 사용하고, 넓은 불투명 제어는 중앙부의 픽셀별 마스크 계산을 제거합니다. 짧고 넓은 중앙부를 캐시 안에서 단순 채우기 하는 편이 빠르면 CPU를 사용하므로 모든 작은 작업을 무조건 가속기로 보내지는 않습니다.

LVGL 코어의 선택적 weak hook(`0002-linux-scroll-reuse-hook.patch`)은 일반 포트의 동작을 유지하면서 Linux 포트에 스크롤 재사용 기회를 줍니다. 한 프레임 사이에 들어온 여러 이동량을 합쳐, 다음 refresh 시작 때 이전 framebuffer의 겹치는 부분을 canvas로 **2D-DMA 한 번으로 복사**합니다. 소스와 목적지는 다른 버퍼이므로 DMA의 겹친 메모리 복사 동작에 의존하지 않습니다. 홀수 열만 CPU로 처리합니다. 새로 노출된 띠·고정 테두리를 렌더링한 뒤 변경 사각형을 합쳐 한 번 전송합니다. 별도 전체 화면 RAM 버퍼는 추가하지 않습니다.

다음 조건을 만족할 때만 재사용합니다.

- 단색 배경 또는 단색 조상 배경이 비치는 투명 컨테이너이며 변환·레이어 투명도·특수 blend가 없습니다. 투명 영역 뒤에 다른 내용이 겹치면 재사용하지 않습니다.
- 가시 영역 안에서 작은 정수 픽셀 이동이며, 부모의 clip 및 둥근 모서리 안쪽을 사용합니다.
- 고정 자식과 사용자 정의 draw callback은 경계와 이동 잔상을 다시 그립니다. 이 영역이 재사용 영역의 절반을 넘거나 24개를 넘으면 전체 재렌더링합니다. 조상 배경의 사용자 그리기나 중앙 overlay 등 안전성을 보장할 수 없는 경우도 전체 재렌더링합니다.
- 복사할 영역에 아직 그리지 않은 내용·스타일 변경이 없습니다. 변경이 있으면 해당 프레임은 전체 재렌더링합니다.

따라서 gradient 배경, 스크롤 위치에 따라 모양이 바뀌는 효과, 지속적으로 갱신되는 복잡한 장면은 기존 렌더링을 사용할 수 있습니다. `KORVO_SCROLL_DISABLE=1`로 픽셀 재사용만 끄고 비교할 수 있습니다. 드라이버가 이전 버전이면 캐시 가능한 canvas의 순서 보장 `memmove`로 호환 동작합니다.

`widgets`의 탭 페이지는 배경 opacity가 0이고, Tabview의 내부 callback이 원래 모든 이벤트에 등록되어 있었습니다. `0003-tabview-register-handled-events.patch`는 실제로 처리하는 `LAYOUT_CHANGED`와 `SCROLL_END`만 등록합니다. 탐색 동작을 유지하면서 draw callback과 구분할 수 있게 했습니다. 실제 Goodix evdev 경로로 widgets 페이지를 위아래로 드래그하여 스크롤 요청 32회를 병합하고 7회의 픽셀 재사용을 확인했습니다. 내용/스타일이 바뀐 프레임은 전체 렌더링으로 전환했습니다.

같은 커널·드라이버에서 이전 렌더러(`023a194e`)와 수정 렌더러를 세 번 교차 실행한 90단계 검사 결과입니다. SSH를 끊고 UART에서 실행했으며 성능 오버레이는 숨겼습니다. CPU는 표시 문자열이 아닌 `getrusage`의 실제 프로세스 user+system 시간입니다.

| 검사 | 이전 CPU 시간 중앙값 | 수정 CPU 시간 중앙값 | 감소 | 경과 시간 중앙값 |
|---|---:|---:|---:|---|
| 슬라이더 | 581ms | 297ms | 약 49% | 약 3.0초 → 3.0초 (30Hz 상한 유지) |
| 스크롤 | 2,835ms | 1,351ms | 약 52% | 3.66초 → 3.03초 (약 25 → 30회/초) |

이 표는 **공통으로 수정된 드라이버 위에서 렌더러 변경을 비교한 값**입니다. 이전 드라이버의 busy-poll/cache 수정 효과까지 더한 수치는 아닙니다. 90단계 중 첫 위치는 그대로이므로 실제 이동은 89회입니다. 메모리가 부족하거나 장면의 구성이 달라지면 결과도 달라집니다. [세 번의 원시 측정값과 검증 기록](../tests/results/interaction-2026-11-19.md)을 함께 제공합니다.

재현 도구는 WSL에서 다음처럼 빌드합니다.

```sh
bash tests/build-interaction-bench.sh
# 같은 검사 코드에 이전 렌더러를 연결한 비교 실행 파일
bash tests/build-interaction-bench.sh "$HOME/esp32-s31-linux-reference/build/br-output-korvo1" 023a194e
```

출력된 실행 파일을 보드에 복사하고 GUI를 멈춘 뒤 실행합니다. CPU 측정 동안 SSH를 유지하면 추가 프로세스의 RAM 때문에 swap/page-in이 증가할 수 있습니다.

```sh
/etc/init.d/S97lvgl-music-demo stop
KORVO_UI_STATS=1 ./interaction-bench slider 90
KORVO_UI_STATS=1 ./interaction-bench scroll 90
KORVO_UI_STATS=1 ./interaction-bench scroll-x 90
/etc/init.d/S97lvgl-music-demo start
```

`KORVO_PPA_PROFILE=1`은 SW task 종류별 시간과 가속기 호출 시간을 기록하는 진단용 옵션입니다. 기본 실행에는 이 계측을 하지 않습니다. `KORVO_PPA_FLUSH_ONLY=1`은 draw unit을 끄고 DMA2D 화면 전송만 유지하는 비교 옵션입니다.

`tests/check-scroll-reuse.sh`는 같은 실행 파일에서 재사용을 켜고 끈 화면을 비교합니다. 세로·가로 이동과 방향 반전, 내용 변경, 여러 입력의 병합, 투명 컨테이너·Tabview·사용자 그리기·고정 자식 및 overlay·gradient의 fallback을 검사하며 모든 캡처의 **384,000픽셀이 정확히 일치**해야 통과합니다. 지원 장면에서 실제 재사용이 일어났는지도 확인합니다. 비교가 끝난 임시 캡처는 삭제하고 실패한 캡처는 남깁니다. 보존하려면 `KORVO_PROOF_KEEP=1`을 설정합니다.

```sh
sh tests/check-scroll-reuse.sh /root/interaction-bench /root/new-scroll-proof
```

## 정확도 검사

### 추가 CPU·메모리 최적화

이전 렌더러 비교 기준은 커밋 `20cf441d`입니다. 네이티브 해상도와 33ms refresh 설정을 유지합니다. 기본 `korvo-ui`/`lvgl-music-demo`는 `korvo_ppa_lvgl_fast.c`를 사용합니다. CPU가 늘어나는 설정을 기본 적용하지 않도록, 아래 캐시·묶음 처리는 별도 `korvo-ui-extra`/`lvgl-music-demo-extra`에 포함했습니다. 두 실행 파일은 함께 설치되며 하나씩 실행해야 합니다.

- **명령 묶기:** draw unit 안에서 함께 처리할 수 있는 최대 16개 fill/A8 blend 요청을 순서대로 전달합니다. 한 개만 남으면 기존 RUN ioctl을 사용합니다. SW task가 끼거나 task가 하나씩 생성되면 단일 요청으로 처리하므로 항상 명령이 합쳐지는 것은 아닙니다. 커널은 실제 요청 수만 복사하며 완료한 개수를 돌려줍니다. SW 렌더링·scratch 재사용·framebuffer 전송 전에 대기 명령을 완료하여 그리기 순서를 보존합니다.
- **반복 그리기 캐시:** 글자/폰트 심벌의 배치와 A8 bitmap, 곡선 마스크를 기존 32KiB scratch 안에서 재사용합니다. 글자는 LVGL의 RGB565 합성 함수를 그대로 사용하여 부분 갱신 경계의 반올림 차이를 피합니다. 선택·장식·이미지 폰트·겹친 glyph 등 지원하지 않는 경우는 기존 글자 렌더러로 돌아갑니다. 이미지 업로드나 용량 초과 시 scratch 캐시를 비웁니다.
- **그림자 모서리 캐시:** `0004-linux-shadow-corner-cache.patch`는 서로 다른 크기/반지름의 모서리 3개를 예약 canvas의 남는 공간에 보관합니다. LVGL이 계산한 원래 mask를 그대로 복사하므로 모양이나 blur 값을 바꾸지 않습니다. 다른 포트는 기존 단일 shadow cache를 유지합니다.
- **작은 변경의 재렌더링:** 내용이 바뀐 영역과 그 영역이 이동한 잔상 범위를 다시 그립니다. 영역이 너무 크거나 복잡하면 전체 렌더링합니다. 입력마다 수행하던 배경/clip 검사도 한 refresh에 한 번 수행합니다.
- **메모리:** 캐시 데이터·메타데이터는 기존 예약 메모리를 사용하며 별도 framebuffer를 늘리지 않습니다. 카메라의 115,200바이트 버퍼는 camera 화면에서만 할당합니다. 함수/데이터 section 제거로 사용하지 않는 코드를 최종 실행 파일에서 제외하고, 음악 플레이어는 고정 5ms polling 대신 LVGL이 요청한 다음 처리 시각을 사용합니다.

데모 음악은 빌드 호스트의 `generate-demo-wav.py`로 만들어 `/usr/share/sounds/korvo-lvgl-demo.wav`에 설치합니다. 보드에서 첫 재생 때 수행하던 software sine 합성과 `/tmp`의 약 1.18MiB 파일을 제거하며 target Python은 필요하지 않습니다. 일반 드래그의 추가 CPU 감소는 반복 측정에서 일관되게 확인되지 않았습니다. [원시 수치·최종 기본값 선택·검증 조건](../tests/results/cpu-2026-11-19.md)을 확인하세요.

확장 실행 파일에서 `KORVO_CACHE_DISABLE=1`은 추가 캐시를 끄고 `KORVO_BATCH_DISABLE=1`은 명령 묶기를 끕니다. `KORVO_UI_STATS=1` 종료 출력의 `korvo-cache`, `korvo-shadow`로 hit/miss와 명령 수를 확인할 수 있습니다. `batch_calls`는 queue를 비운 횟수이며 단일 RUN도 포함합니다. `dirty_reused`는 작은 내용 변경 중에도 픽셀을 재사용한 횟수이며 기본 모드에서도 적용합니다.

작업이 없는 draw unit이 0(작업 중)을 반환하던 문제도 기본·확장 모드에서 수정했습니다. 이제 `LV_DRAW_UNIT_IDLE`을 반환하여 offscreen canvas/vector 초기화가 무한 대기로 들어가지 않습니다.

드라이버의 `SOURCE_CLEAN` flag는 전용 scratch의 A8 source를 처음에 전체 동기화한 뒤, **CPU가 해당 mask를 수정하지 않은 경우에만** 사용할 수 있습니다. 포트는 완료된 전송 후에만 clean 상태를 기록하고 scratch 재사용 시 초기화합니다. 이는 다른 프로세스나 임의의 사용자 버퍼에 대한 cache 일관성 보장이 아닙니다.

```sh
/etc/init.d/S97lvgl-music-demo stop
ppa-test
/etc/init.d/S97lvgl-music-demo start
```

`ppa-test`는 stride/crop 복사, 좁은 사각형의 캐시 동기화, 홀수/짝수 X 좌표의 framebuffer 재사용, RGB565 5색 채우기, 고정 alpha·ARGB8888·A8 합성, 범위 초과 요청 거부를 검사합니다. CPU로 만든 기준값과 픽셀을 비교하며 대상 사각형 밖의 보존도 확인합니다. 기본 검사는 LCD 내용을 바꾸지 않습니다. GUI 실행 중에는 전용 workspace 소유권 때문에 `EBUSY`가 정상이며 `--stats`는 동시 실행 가능합니다.

`tests/ppa-lvgl-scene.c`는 실제 LVGL draw dispatch와 flush, offscreen ARGB canvas 완료, 그림자 cache hit까지 검사합니다. 기본 모드는 `korvo_ppa_lvgl_fast.c`, 확장 모드는 `korvo_ppa_lvgl.c`와 빌드하고 LVGL/ThorVG를 링크합니다. GUI를 멈춘 뒤 소프트웨어와 하드웨어로 각각 캡처합니다.

```sh
KORVO_PPA_DISABLE=1 ./ppa-lvgl-scene /root/ppa-sw.rgb565
./ppa-lvgl-scene /root/ppa-hw.rgb565
```

`KORVO_PPA_DEVICE_TEST=1 ./ppa-lvgl-scene /root/ppa-hw.rgb565`는 장면을 유지한 상태에서 `ppa-test`도 실행합니다. 이를 통해 균일한 콘솔 배경 대신 무늬가 있는 실제 framebuffer를 소스로 사용하여 재사용 복사를 검사할 수 있습니다.

파일을 호스트로 가져와 비교합니다.

```sh
python3 tests/compare-ppa-scene.py ppa-sw.rgb565 ppa-hw.rgb565
```

전체 384,000픽셀을 확인하며 불투명 영역은 정확히 일치해야 합니다. alpha 합성 영역에서만 하드웨어 반올림 차이로 채널당 RGB565 한 단계 이내를 허용합니다. `tests/check-ui-input.sh`는 별도로 메뉴·widgets·audio·camera의 짧은 터치와 HOME 복귀를 검사합니다.

## 소스와 재현

드라이버/UAPI/진단 도구/LVGL 포트는 `korvo-addons/package/korvo-ppa/src/`에 있습니다. Buildroot는 커널 모듈과 `ppa-test`를 microSD rootfs에 설치합니다. DT의 canvas/descriptor 예약이 새 커널에 들어가므로 기존 보드를 업데이트할 때는 GUI 실행 파일뿐 아니라 커널과 모듈도 함께 적용해야 DIRECT 경로가 활성화됩니다. 예전 DT에서는 32KiB 타일 방식으로 동작할 수 있습니다.

`build-korvo-wsl.sh`로 공개 이미지와 checksum을 재생성할 수 있습니다. 개인 Wi-Fi 설정과 SSH private key는 이미지에 포함하지 않습니다.

## 원본 참고 자료

- [Korvo-1 RGB panel과 LVGL 연결](https://github.com/espressif/esp-bsp/blob/master/bsp/esp32_s31_korvo_1/src/bsp_display.c)
- [공식 Korvo-1 LVGL 데모 설정](https://github.com/espressif/esp-bsp/blob/master/examples/display_lvgl_demos/sdkconfig.bsp.esp32_s31_korvo_1)
- [esp_lvgl_port PPA 옵션](https://github.com/espressif/esp-bsp/blob/master/components/esp_lvgl_port/Kconfig)
- [S31 PPA 사용 설명](https://docs.espressif.com/projects/esp-idf/en/v6.1.1/esp32s31/api-reference/peripherals/ppa.html)
- [S31 PPA low-level 구현](https://github.com/espressif/esp-idf/blob/master/components/esp_hal_ppa/esp32s31/include/hal/ppa_ll.h)
- [S31 2D-DMA low-level 구현](https://github.com/espressif/esp-idf/blob/master/components/esp_hal_dma/esp32s31/include/hal/dma2d_ll.h)
