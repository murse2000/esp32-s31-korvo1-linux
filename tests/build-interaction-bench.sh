#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
project_source="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
br_output="${1:-$HOME/esp32-s31-linux-reference/build/br-output-korvo1}"
revision="${2:-}"
port_opt="${KORVO_PORT_OPT:--O2}"
build_dir="$br_output/build/korvo-interaction-bench${revision:+-reference}"
if [[ "${KORVO_PORT_VARIANT:-fast}" == extra ]]; then build_dir="$build_dir-extra"; fi
staging="$br_output/host/riscv32-buildroot-linux-musl/sysroot/usr"
cc="$br_output/host/bin/riscv32-buildroot-linux-musl-gcc"
cxx="$br_output/host/bin/riscv32-buildroot-linux-musl-g++"
mkdir -p "$build_dir"
port="$project_source/korvo-addons/package/korvo-ppa/src/korvo_ppa_lvgl_fast.c"
if [[ "${KORVO_PORT_VARIANT:-fast}" == extra ]]; then
  port="$project_source/korvo-addons/package/korvo-ppa/src/korvo_ppa_lvgl.c"
fi
extra_objects=()
if [[ -n "$revision" ]]; then
  git -C "$project_source" show "$revision:korvo-addons/package/korvo-ppa/src/korvo_ppa_lvgl.c" > "$build_dir/reference.c"
  port="$build_dir/reference.c"
  if ! grep -q '^uint32_t korvo_linux_cpu_idle' "$port"; then
    printf '#include <lvgl.h>\nuint32_t korvo_linux_cpu_idle(void) { return lv_timer_get_idle(); }\n' > "$build_dir/idle.c"
    "$cc" -O2 -I"$staging/include/lvgl" -c "$build_dir/idle.c" -o "$build_dir/idle.o"
    extra_objects+=("$build_dir/idle.o")
  fi
fi
"$cc" -O2 -Wall -Wextra -I"$staging/include/lvgl" \
  -I"$project_source/korvo-addons/package/korvo-ppa/src" \
  -I"$project_source/korvo-addons/package/korvo-camera/src" \
  -c "$project_source/tests/interaction-bench.c" -o "$build_dir/interaction.o"
"$cc" "$port_opt" -Wall -Wextra -I"$staging/include/lvgl" \
  -I"$project_source/korvo-addons/package/korvo-ppa/src" -c "$port" -o "$build_dir/port.o"
"$cxx" "$build_dir/interaction.o" "$build_dir/port.o" "${extra_objects[@]}" -L"$staging/lib" \
  -Wl,--start-group -llvgl -llvgl_thorvg -Wl,--end-group -lm -lpthread \
  -o "$build_dir/interaction-bench"
printf '%s\n' "$build_dir/interaction-bench"
