#!/bin/sh
set -eu

repo_root="$(CDPATH="" cd -- "$(dirname -- "$0")/.." && pwd)"
weact_dir="${WEACT_DIR:-$HOME/src/WeActStudio.EpaperModule}"
example_dir="$weact_dir/Example/EpaperModuleTest_RaspberryPi"
build_dir="${TMPDIR:-/tmp}/epd-weact-2.9-bw-build"
spi_speed_hz="${EPD_SPI_SPEED_HZ:-4000000}"

case "$spi_speed_hz" in
'' | *[!0-9]*)
  echo "EPD_SPI_SPEED_HZ must be a positive integer" >&2
  exit 1
  ;;
0)
  echo "EPD_SPI_SPEED_HZ must be greater than zero" >&2
  exit 1
  ;;
esac

if ! command -v gcc >/dev/null 2>&1; then
  echo "gcc is required: sudo apt install -y build-essential" >&2
  exit 1
fi

if [ ! -d "$example_dir/epaper" ]; then
  if ! command -v git >/dev/null 2>&1; then
    echo "git is required to fetch WeAct checkout" >&2
    exit 1
  fi

  echo "Fetching missing WeAct checkout: $weact_dir" >&2
  mkdir -p "$(dirname "$weact_dir")"
  rm -rf "$weact_dir"
  git clone --depth 1 https://github.com/WeActStudio/WeActStudio.EpaperModule.git "$weact_dir"
fi

if [ ! -d "$example_dir/epaper" ]; then
  echo "Missing WeAct Raspberry Pi example after clone: $example_dir" >&2
  exit 1
fi

mkdir -p "$HOME/bin"
rm -rf "$build_dir"
mkdir -p "$build_dir"
cp -R "$example_dir/epaper" "$build_dir/epaper"
awk -v speed="$spi_speed_hz" '
  {
    gsub(/wiringPiSPISetupMode\(SPI_CHANNEL, 1000000, mode\)/,
         "wiringPiSPISetupMode(SPI_CHANNEL, " speed ", mode)");
    gsub(/printf\("EPD_H: %d, EPD_W: %d, epd_type: %d\\n", EPD_H, EPD_W, epd_type\);/,
         "(void)0;");
    gsub(/printf\("set wiringPi lib success!\\r\\n"\);/,
         "(void)0;");
    gsub(/printf\("spi setup success!\\r\\n"\);/,
         "(void)0;");
    print;
  }
' "$example_dir/epaper/epaper.c" >"$build_dir/epaper/epaper.c"

cd "$example_dir"

build_one() {
  src="$1"
  out="$2"

  gcc -Wall -Wextra -O2 -ffunction-sections -fdata-sections \
    -DEPD290_BW -I"$build_dir/epaper" \
    "$src" "$build_dir"/epaper/*.c \
    -Wl,--gc-sections -o "$out" -lwiringPi -lm

  echo "Built: $out"
}

build_one "$repo_root/epaper/weact-2.9-bw/main_fast_partial.c" \
  "$HOME/bin/epd_290_bw_fast_partial"
build_one "$repo_root/epaper/weact-2.9-bw/main_status_screen.c" \
  "$HOME/bin/epd_290_bw_status"
echo "SPI speed: $spi_speed_hz Hz"
