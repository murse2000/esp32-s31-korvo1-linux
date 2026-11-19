# 스크롤·슬라이더 보드 측정 (2026-11-19)

## 조건

- ESP32-S31-Korvo-1, Linux 7.3.4 `#7`, Hart 1, 800×480 RGB565 DIRECT canvas.
- 같은 커널·IRQ 기반 PPA/2D-DMA 드라이버와 LVGL 9.2.2 라이브러리를 양쪽에서 사용.
- 이전 포트는 커밋 `023a194e`의 `korvo_ppa_lvgl.c`, 수정 포트는 스크롤 픽셀 재사용/A8/채우기 최적화를 포함.
- 같은 `interaction-bench.c`를 사용하고 이전/수정 실행을 번갈아 세 번 수행.
- UART에서 실행하고 SSH 연결은 종료. 계측용 오버레이는 숨김. SW task별 추가 프로파일러는 끔.
- 루프는 90개 위치를 33,333µs 간격으로 요청. 첫 위치는 변하지 않으므로 실제 이동/값 변경은 89회.
- CPU 시간은 `getrusage(RUSAGE_SELF)`의 user+system 합계. DMA/디스크 대기와 프로세스 외부 작업은 이 값에 포함되지 않음.
- IRQ/캐시 수정은 두 실행에 공통이므로, 아래 표는 렌더러 변경 효과를 비교함. 기존 polling 드라이버 대비 전체 개선율은 아님.

## 원시 수치

단위는 µs입니다. `wall`은 대기까지 포함한 전체 경과 시간이며 `render`는 루프의 값 변경과 refresh 처리에 걸린 시간입니다.

| 동작 | 회차 | 이전 CPU | 수정 CPU | 이전 wall | 수정 wall | 이전 render | 수정 render |
|---|---:|---:|---:|---:|---:|---:|---:|
| slider | 1 | 580940 | 316129 | 3003692 | 3007237 | 799501 | 664927 |
| slider | 2 | 626252 | 296962 | 3008952 | 3008276 | 1159731 | 501359 |
| slider | 3 | 571692 | 287565 | 3009662 | 3004383 | 715490 | 475819 |
| scroll | 1 | 2835379 | 1351072 | 3660845 | 3030647 | 3616437 | 2188509 |
| scroll | 2 | 2809834 | 1438940 | 3648165 | 3211702 | 3607711 | 3175087 |
| scroll | 3 | 2941716 | 1338575 | 5001948 | 3003364 | 4963300 | 2172061 |

각 열의 중앙값으로 계산하면 slider CPU 시간은 580940 → 296962µs(약 49%), scroll CPU 시간은 2835379 → 1351072µs(약 52%) 감소합니다. Scroll의 경과 시간은 3660845 → 3030647µs이며, 루프 진행률은 약 25 → 30회/초입니다. Slider는 양쪽 모두 30Hz 요청 상한을 유지했습니다.

진단 출력의 프로세스 CPU 비율 중앙값은 slider 19 → 9%, scroll 77 → 44%였습니다. 각 열은 독립적인 중앙값이며 특정 한 실행을 의미하지 않습니다. 메모리와 캐시 상태에 따른 변동을 숨기지 않도록 세 번의 값을 모두 제공합니다. Scroll 이전 회차 3과 수정 회차 2는 각각 major page fault가 1회 있었고, 다른 측정은 0회였습니다.

수정 scroll 실행 세 번 모두 다음을 기록했습니다.

```text
korvo-scroll: calls=89 queued=89 dirty=0 moves=89 reused_pixels=23068088
korvo-ppa: copy=89 fill=269 flush=90 blend=0 errors=0
```

## 화면 정확도

`check-scroll-reuse.sh`는 동일한 새 실행 파일에서 재사용을 끈 기준 화면과 재사용 화면을 비교합니다. 검사는 RGB565 framebuffer 전체 384,000픽셀을 `cmp`로 대조하며 허용 오차를 주지 않습니다. 캡처 위치에는 정방향 이동, 방향 반전 직전/직후가 포함됩니다.

최종 검증에서 다음 장면은 전부 정확히 일치했습니다. 지원 장면에서 실제 재사용 명령이 실행됐는지도 검사했습니다.

```text
PASS vertical: all captured frames match exactly
PASS horizontal: all captured frames match exactly
PASS changed: all captured frames match exactly
PASS coalesced: all captured frames match exactly
PASS transparent: all captured frames match exactly
PASS tabview: all captured frames match exactly
PASS custom: all captured frames match exactly
PASS floating: all captured frames match exactly
PASS overlay: all captured frames match exactly
PASS gradient: all captured frames match exactly
```

Coalesced 검사는 입력 267회를 89번의 이동/갱신으로 병합했습니다. 내용 변경 검사는 변경된 한 프레임에서 재사용을 건너뛰고 전체 렌더링했습니다. 예측할 수 없는 배경/겹침은 전체 렌더링을 유지합니다.

실제 `korvo-ui widgets`에도 Goodix evdev 장치를 통해 위/아래 drag를 전달했습니다. 투명 탭 페이지에서 `calls=32 queued=32 dirty=3 moves=7 reused_pixels=1732040`을 기록했습니다. 고정 자식과 사용자 그리기 영역은 다시 그리며, 내용을 바꾼 프레임은 전체 렌더링합니다. 이 검사는 동작/경로 확인용이고 추가 프로파일러와 SSH가 켜져 있어 위 성능 표에 합치지 않았습니다.

별도의 `ppa-lvgl-scene.c` 검사는 순수 SW 렌더링과 PPA 렌더링을 비교합니다. 이 경우 alpha 합성의 하드웨어 양자화 때문에 지정된 반투명/곡선 영역에서만 채널당 RGB565 한 단계 이내를 허용하며, 스크롤 재사용의 무오차 비교와 구분합니다.

## 장치·통합 검사

무늬가 있는 LVGL 장면을 유지한 상태에서 `KORVO_PPA_DEVICE_TEST=1`로 실행한 장치 검사는 다음을 통과했습니다.

```text
DMA2D cropped/strided RGB565 copy and surrounding pixels: OK
Narrow rectangle cache synchronization and surrounding pixels: OK
Framebuffer reuse, even/odd source X and untouched surrounding pixels: OK
PPA RGB565 red/green/blue/white/black fills: OK
PPA RGB565 / ARGB8888 / supported A8 alpha blends: OK
Out-of-bounds request rejected: OK
```

이 시점의 누적 하드웨어 전송 통계는 copy=1547, fill=15174, flush=10473, blend=8770, errors=0이었습니다. 순수 SW/PPA 고정 장면은 10,736개 alpha 픽셀의 차이가 허용 범위 안이었고, 나머지 픽셀은 정확히 일치했습니다.

최종 메뉴 통합 검사는 짧은 터치의 큐 처리, widgets/HOME, audio/HOME/camera/HOME을 통과했습니다. 별도 카메라 명령은 유효한 240×240 사진 1장을 저장했습니다. 8초 benchmark가 끝난 뒤에도 errors=0, native 800×480 framebuffer refresh 활성, ES8389 사운드카드 등록을 확인했습니다.

배포 이미지의 최종 부팅 버전은 커널 `#11`입니다. 921600 baud로 기록하고 Wi-Fi 자동 재접속, 메뉴 자동 실행, DMA2D 완료 인터럽트, 정상 CPU monitor와 오류 카운터 0을 확인했습니다. 모듈·GUI·음악 플레이어·진단 도구의 보드/Buildroot SHA-256이 일치했습니다. 위 성능 수치는 `#7`에서 측정했으며 이후 커널 빌드의 변경은 같은 DTS를 복사하여 재빌드한 버전 정보입니다.

## 재현

```sh
bash tests/build-interaction-bench.sh
bash tests/build-interaction-bench.sh "$HOME/esp32-s31-linux-reference/build/br-output-korvo1" 023a194e
```

출력된 실행 파일을 각각 보드의 `interaction-bench.after`, `interaction-bench.before`로 복사합니다. UART에서 GUI를 멈추고 before/after를 교차해 세 번 실행합니다. `KORVO_UI_STATS=1`을 지정하면 하드웨어/재사용 명령 수도 함께 나옵니다.

```sh
/etc/init.d/S97lvgl-music-demo stop
KORVO_UI_STATS=1 ./interaction-bench.before scroll 90
KORVO_UI_STATS=1 ./interaction-bench.after scroll 90
/etc/init.d/S97lvgl-music-demo start
```

이는 고정된 검사 장면의 결과입니다. 모든 사용자 화면이나 복잡한 애니메이션의 일정한 FPS를 보장하지 않습니다. 현재 CPU 오버레이는 `/proc/stat`의 Linux CPU 계측을 사용하고, 위 비교는 프로세스의 실제 CPU 시간을 사용합니다.
