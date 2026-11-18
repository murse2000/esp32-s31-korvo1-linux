#!/usr/bin/env bash
set -euo pipefail

# WSL appends Windows PATH entries; Buildroot rejects paths containing spaces.
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

project_source="${1:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)}"
work="${HOME}/esp32s31-korvo"
reference="${HOME}/esp32-s31-linux-reference"
idf_path="${HOME}/esp/esp-idf"
ref_commit="e27b61feb92bc0a6d734c8e5cdb0b6af421c9817"

mkdir -p "$work"
if [[ ! -d "$reference/.git" ]]; then
    git clone --depth 1 --recurse-submodules https://github.com/annoyedmilk/esp32-s31-linux.git "$reference"
fi
if [[ ! -f "$reference/br2-external/board/esp32s31/linux-sdroot-korvo1.fragment" ]]; then
    # Reapply the pinned project patch when a WSL checkout still has the old
    # flash-root profile. Build caches live under ignored build/ and survive.
    git -C "$reference" fetch --depth 1 origin "$ref_commit"
    git -C "$reference" checkout --detach "$ref_commit"
    git -C "$reference" reset --hard "$ref_commit"
    git -C "$reference" submodule update --init --recursive
    git -C "$reference" apply "$project_source/korvo1-nor-reference.patch"
fi

git -C "$reference" submodule update --init --recursive
if ! grep -q 'HOST_UTIL_LINUX_AUTORECONF = NO' "$reference/external/buildroot/package/util-linux/util-linux.mk"; then
    git -C "$reference/external/buildroot" apply "$project_source/buildroot-host-util-linux-fix.patch"
fi
if ! grep -q -- '--with-curses=ncurses' "$reference/external/buildroot/package/alsa-utils/alsa-utils.mk"; then
    git -C "$reference/external/buildroot" apply "$project_source/buildroot-alsa-ncurses.patch"
fi
if ! grep -q 'This disk-root image currently ships only the narrow target ncurses ABI' "$reference/external/buildroot/package/util-linux/util-linux.mk"; then
    git -C "$reference/external/buildroot" apply "$project_source/buildroot-util-linux-narrow-ncurses.patch"
fi
if ! grep -q 'LINUX_APPLY_ESP32S31_OVERLAY' "$reference/external/buildroot/linux/linux.mk"; then
    git -C "$reference/external/buildroot" apply "$project_source/buildroot-linux-esp32s31-overlay.patch"
fi
if ! command -v gcc-14 >/dev/null || ! command -v g++-14 >/dev/null; then
    if [[ $EUID -eq 0 ]]; then
        apt-get update
        DEBIAN_FRONTEND=noninteractive apt-get install -y gcc-14 g++-14
    else
        sudo apt-get update
        sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y gcc-14 g++-14
    fi
fi

export IDF_PATH="$idf_path"
if [[ ! -x "$IDF_PATH/tools/idf.py" ]] || \
   ! compgen -G "$HOME/.espressif/tools/riscv32-esp-elf/esp-*/riscv32-esp-elf/bin/riscv32-esp-elf-gcc" >/dev/null; then
    cd "$reference"
    IDF_PATH="$IDF_PATH" scripts/ci-setup.sh
fi

export HOSTCC=/usr/bin/gcc-14 HOSTCXX=/usr/bin/g++-14
cd "$reference"
# Buildroot does not notice changes to the external Linux overlay once the
# kernel source has been extracted. Sync the release overlay before building,
# and force a fresh Linux extract only when that source set changes.
for d in Documentation arch drivers include; do
    mkdir -p "$reference/linux/$d"
    cp -a "$project_source/linux/$d/." "$reference/linux/$d/"
done
overlay_hash="$(find "$project_source/linux" -type f ! -name '._*' -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -d' ' -f1)"
overlay_stamp="$reference/build/.korvo1-public-linux-overlay"
if [[ -f "$reference/build/br-output-korvo1/.config" ]] && \
   [[ "$(cat "$overlay_stamp" 2>/dev/null || true)" != "$overlay_hash" ]]; then
    make -C "$reference/external/buildroot" \
        O="$reference/build/br-output-korvo1" \
        HOSTCC=/usr/bin/gcc-14 HOSTCXX=/usr/bin/g++-14 \
        linux-dirclean linux-headers-dirclean
fi
mkdir -p "$(dirname "$overlay_stamp")"
printf '%s\n' "$overlay_hash" > "$overlay_stamp"
BOARD=korvo1 IDF_PATH="$IDF_PATH" BR_JOBS="${BR_JOBS:-8}" \
    BR_OUTPUT="$reference/build/br-output-korvo1" \
    BR_DL_DIR="$reference/build/br-dl" scripts/ci-build.sh

python_env="$(find "$HOME/.espressif/python_env" -mindepth 3 -maxdepth 3 -executable -path '*/bin/python' | sort | tail -1)"
[[ -x "$python_env" ]] || { echo 'ESP-IDF Python environment not found' >&2; exit 1; }
flash_args=( $(make -s flash-args BOARD=korvo1) )
out_dir="$project_source/build-output"
mkdir -p "$out_dir"
"$python_env" -m esptool --chip esp32s31 merge-bin \
    --format raw --flash-mode dio --flash-freq 80m --flash-size 16MB \
    --pad-to-size 16MB -o "$out_dir/korvo1-flash.bin" "${flash_args[@]}"
cp build/korvo1/Image "$out_dir/korvo1-linux-Image"
cp build/korvo1/linux.size "$out_dir/korvo1-linux-size.bin"
cp build/korvo1/bootloader/partition_table/partition-table.bin "$out_dir/korvo1-partition-table.bin"
cp bootloader/partitions-korvo1.csv "$out_dir/korvo1-partitions.csv"
# The target has very little RAM. Keep the decoder dictionary small so the
# board can stream/decompress this image without xz being OOM-killed.
xz -T1 -0 --lzma2=dict=1MiB --check=crc64 -c \
    build/korvo1/buildroot/sdcard.img > "$out_dir/korvo1-sdcard.img.xz"
# Keep checksums synchronized with both current and retained recovery artifacts.
(
    cd "$out_dir"
    find . -type f ! -name SHA256SUMS -printf '%P\0' | sort -z | xargs -0 sha256sum > SHA256SUMS
)

printf '\nFlash image: %s/korvo1-flash.bin\nSD card image: %s/korvo1-sdcard.img.xz\n' "$out_dir" "$out_dir"
if [[ -n "${FLASH_PORT:-}" ]]; then
    "$python_env" -m esptool --chip esp32s31 -p "$FLASH_PORT" -b 921600 \
        write-flash 0x0 "$out_dir/korvo1-flash.bin"
fi
