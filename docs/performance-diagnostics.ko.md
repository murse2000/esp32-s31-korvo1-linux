# 성능 진단: ping·UART 콘솔 체감 지연의 원인

이 문서는 "ping만 실행해도 CPU 사용률이 크게 흔들리고 UART 콘솔이 느리다"는
증상을 보드에서 직접 측정해 정리한 기록입니다. 측정은 SSH 세션을 닫은 상태에서
COM3 UART로만 수행했습니다.

## 측정 환경

- Linux 7.3.4 #11 PREEMPT, Linux CPU 320 MHz
- `MemTotal` 9212 kB, zram 6140 kB(priority 100), microSD swap 파일 32 MiB(priority 5)
- SD ext4 rootfs, read_ahead 기본 128 KiB
- LCD 800×480 RGB565, PPA/DMA2D 렌더링
- UART `ttyS0` 115200 8N1, 콘솔

측정 도구는 보드에 넣은 셸 스크립트와 320 MHz에서 포크 없이 `/proc`를 읽는
정적 RISC-V 바이너리입니다. 지표는 `/proc/stat`, `/proc/interrupts`,
`/proc/vmstat`, `/sys/block/mmcblk0/stat`의 5~8초 델타입니다.

## 먼저 기각된 가설

### USB host IRQ

USB A/B 테스트 결과입니다. HID 키보드(04b4:0510)를 `authorized=0`으로
비활성화했다가 복원했습니다.

| 상태 | nonidle | system | USB IRQ/s |
| --- | --- | --- | --- |
| 연결 | 15.6% | 7.9% | 3491 |
| 비활성 | 17.6% | 8.5% | 0 |

초당 약 3,500회의 USB IRQ를 없애도 CPU 사용률은 줄지 않았습니다.
`/proc/stat`의 `irq` 항목도 0.0%로, 이 IRQ는 CPU를 거의 쓰지 않습니다.
USB host 기능은 그대로 유지합니다.

### Wi-Fi 절전과 UART 드라이버

- `iw dev wlan0 power_save`는 이미 `off`였습니다.
- `esp32_uart.c`의 콘솔 경로(`esp32_uart_put_char_sync`)는 TX FIFO 여유를
  1 µs 간격으로 폴링합니다. 이는 115200 baud에서 FIFO가 가득 찼을 때만
  대기하는 통상적인 구현이며, 대기 시간은 전송 시간에 비례합니다.
- 콘솔 입력이 없을 때 UART IRQ는 0회/초였습니다.

즉 UART 드라이버 결함이나 Wi-Fi 절전이 원인이 아닙니다. 115200 baud에서
초당 11.5 kB가 물리적 상한이라는 점은 별개로 남습니다.

## 확인된 원인

이 보드는 사용 가능한 RAM이 9 MiB뿐입니다. 상주 프로세스와 LVGL이 코드·데이터
페이지를 계속 만지는데 page cache가 2 MiB 안팎으로 줄어 있어, 커널이 같은
페이지를 버리고 다시 읽는 순환이 발생합니다.

1. `pgscan_kswapd`가 초당 200~2,600페이지, `pgsteal_direct`도 상시 발생합니다.
2. 기본 readahead 128 KiB가 이 순환을 증폭합니다. 읽은 페이지가 곧 회수되어
   같은 섹터를 다시 읽습니다.
3. 결과적으로 SD rootfs에서 초당 2~6 MiB를 읽고, `io_ticks` 기준 SD 사용률이
   60~86%까지 올라갑니다.
4. SD 읽기와 kswapd가 `system` 시간과 `iowait`를 만들고, `iowait`에 머무는
   프로세스가 load average를 끌어올립니다.

1초 해상도 추적으로 확인한 사실:

- SD 읽기와 kswapd 스캔은 8~9초 간격으로 burst를 이룹니다.
- burst 시점의 페이지 폴트 최대 기여자는 `korvo-ui`(40~62%)이고,
  `mdnsd`, `wpa_supplicant`, `klogd`가 뒤를 잇습니다.
- `korvo-ui`를 `SIGSTOP`한 상태에서도 burst는 계속됩니다. LVGL 단독 원인이
  아니라 시스템 전체의 cache 부족이 원인입니다.

## ping과의 관계

`ping` 자체의 비용은 작습니다.

| 시나리오 | nonidle | system | iowait | SD 읽기 |
| --- | --- | --- | --- | --- |
| idle | 12.1% | 6.6% | 3.2% | 97 IRQ/s |
| 게이트웨이 ping 8회 | 13.6% | 8.3% | 3.5% | 117 IRQ/s |
| 8.8.8.8 ping 8회 | 49.2% | 27.5% | 14.0% | 527 IRQ/s |
| 8.8.8.8 ping 8회 + 콘솔 출력 | 17.5% | 12.7% | 2.2% | 229 IRQ/s |

같은 ping을 콘솔로 출력한 쪽이 더 낮았습니다. 즉 세 번째 행의 49.2%는 ping
때문이 아니라 같은 시점에 겹친 SD/kswapd burst 때문입니다. 사용자가 본
"ping만 해도 CPU가 흔들린다"는 현상은 이 burst와 일치합니다.

## 적용한 변경

`S06readahead` init 스크립트로 MMC readahead를 128 KiB에서 8 KiB로 낮췄습니다.

```sh
for dev in /sys/block/mmcblk*/queue/read_ahead_kb; do
  [ -w "$dev" ] && echo 8 > "$dev"
done
```

### 측정 결과

같은 시간대에 8 KiB와 128 KiB를 교대 측정했습니다.

| readahead | nonidle | iowait | SD 읽기 | kswapd 스캔 |
| --- | --- | --- | --- | --- |
| 8 KiB | 43.9% | 11.0% | 1.6 MB/s | 789/s |
| 128 KiB | 46.3% | 16.0% | 5.3 MB/s | 2075/s |
| 8 KiB | 30.8% | 2.4% | 1.3 MB/s | 530/s |
| 128 KiB | 79.5%* | 1.6% | 1.8 MB/s | 658/s |

\* 마지막 행은 같은 구간에 PPA IRQ가 초당 599회로 치솟아(화면 상호작용)
CPU 수치를 비교에 쓸 수 없습니다.

재부팅으로 `S06readahead`가 실제 부팅 경로에서 동작하는 것을 확인한 뒤
다시 교대 측정했습니다.

| readahead | nonidle | system | iowait | SD 읽기 | kswapd 스캔 |
| --- | --- | --- | --- | --- | --- |
| 128 KiB | 40.6% | 24.4% | 11.0% | 2.7 MB/s | 1224/s |
| 8 KiB | 34.0% | 20.2% | 6.7% | 0.73 MB/s | 378/s |

부팅 직후 5회 연속 표본에서 8 KiB 상태는 SD 읽기 0.15~0.54 MB/s,
kswapd 스캔 131~315/s, `iowait` 1.4~4.5%였습니다.

반복 측정에서 일관된 것은 SD 읽기량과 kswapd 스캔의 감소입니다. CPU 사용률은
burst 위상에 따라 편차가 커서, readahead만으로 일정한 CPU 감소율을 주장하지
않습니다.

## 되돌린 변경

`korvo-ui`의 LVGL idle 루프 상한을 10 ms에서 33 ms로, 입력 폴링을 10 ms에서
20 ms로 바꿔 A/B/A로 비교했습니다.

| 순서 | 구성 | nonidle 평균 |
| --- | --- | --- |
| A | idle 33 ms / 입력 20 ms | 31.0% |
| B | 원래 (10 ms / 10 ms) | 25.5% |
| C | A와 동일 | 30.3% |

이득이 확인되지 않아 소스와 보드 바이너리를 원래대로 되돌렸습니다.

## mdnsd 중복 실행

상주 프로세스를 확인하던 중 `mdnsd`가 두 개 떠 있는 것을 발견했습니다. 부팅
7.9초에 시작한 인스턴스와, 약 21분 뒤에 생긴 두 번째 인스턴스가 각각
416~424 kB를 점유해 약 420 kB를 낭비하고 있었습니다.

원인은 `mdnsd`가 **자체 PID 파일을 지우는 동작**입니다. `/etc/init.d/S50mdnsd`는
`start-stop-daemon --pidfile`로 중복 실행을 막는데, PID 파일이 사라진 상태에서
start가 한 번 더 호출되면 이미 실행 중인 인스턴스를 보지 못하고 새로 띄웁니다.
실제로 조사 시점에 `/var/run/mdnsd.pid`는 없는데 프로세스는 살아 있었습니다.

`S50mdnsd`를 프로세스 기준으로 판단하도록 바꿨습니다.

```sh
start() {
	pid=$(pidof "$DAEMON")
	if [ -n "$pid" ]; then
		echo "$pid" > "$PIDFILE"      # keep stop() working
		logger -t mdnsd-guard "start skipped, already running as $pid"
		return 0
	fi
	...
}
```

`stop()`은 PID 파일로 한 번 종료한 뒤, 남아 있는 동명 프로세스를 이름으로
정리합니다.

검증 결과입니다.

| 시나리오 | 결과 |
| --- | --- |
| start를 연속 2회 호출 | "already running (pid 1468)", 프로세스 1개 유지 |
| PID 파일 삭제 후 start 2회 | 중복 없음, PID 파일 재생성 |
| stop → start | 프로세스 1개, PID 파일 일치 |
| 재부팅 후 | 프로세스 1개(pid 181), PID 파일 181 |

## 남은 과제

- 9 MiB라는 RAM 자체가 상한입니다. readahead 조정은 증폭을 줄이는 조치이지
  근본 해결이 아닙니다.
- 커널은 이미 `CC_OPTIMIZE_FOR_SIZE`와 `LD_DEAD_CODE_DATA_ELIMINATION`이
  적용되어 있어, 기능을 유지하며 회수 가능한 것은 내장 규제 DB 131 kB
  정도입니다.
- `ppa-canvas` 768 KiB는 PPA 가속의 렌더 타깃이고 프레임버퍼는 flush 목적지로만
  허용되므로(`esp32s31_ppa.c`), 제거하면 LVGL이 소프트웨어 렌더로 후퇴합니다.
- 상주 프로세스와 커널 slab 2.5 MiB가 다음 검토 대상입니다.
