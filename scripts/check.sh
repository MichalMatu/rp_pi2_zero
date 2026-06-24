#!/bin/sh
set -eu

repo_root="$(CDPATH="" cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$repo_root"

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

section() {
  printf '\n==> %s\n' "$*"
}

need_tool() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "missing required tool: $1" >&2
    exit 1
  fi
}

run() {
  section "$*"
  "$@"
}

write_epaper_stubs() {
  cat >"$tmpdir/epaper.h" <<'EOF'
#pragma once
#include <stdint.h>

#define EPD_COLOR_WHITE 0
#define EPD_COLOR_BLACK 1
#define EPD_ROTATE_0 0
#define EPD_FONT_SIZE24x12 1
#define EPD_FONT_SIZE16x8 2
#define EPD_FONT_SIZE12x6 3
#define EPD_FONT_SIZE8x6 4
#define EPD_DEEPSLEEP_MODE1 1
#define EPD213_219 0
#define EPD_W 296
#define EPD_H 128
#define EPD_W_BUFF_SIZE 50

void epd_io_deinit(void);
void epd_io_init(void);
void epd_set_panel(int panel, int height, int width);
void epd_paint_newimage(uint8_t *image, int width, int height, int rotate, int color);
void epd_paint_selectimage(uint8_t *image);
void epd_paint_clear(int color);
void epd_paint_showString(uint16_t x, uint16_t y, uint8_t *text, uint16_t font, int color);
uint8_t epd_init(void);
uint8_t epd_init_fast(void);
uint8_t epd_init_partial(void);
void epd_displayBW(uint8_t *image);
void epd_displayBW_fast(uint8_t *image);
void epd_displayBW_partial(uint8_t *image);
void epd_enter_deepsleepmode(int mode);
EOF

  cat >"$tmpdir/wiringPi.h" <<'EOF'
#pragma once
static inline void delay(unsigned int ms) { (void)ms; }
EOF
}

check_shell() {
  set -- \
    scripts/build-epaper-weact-2.9-bw.sh \
    scripts/check.sh \
    scripts/install-git-hooks.sh \
    scripts/hooks/pre-commit

  run sh -n "$@"
  run shellcheck "$@"
  run shfmt -i 2 -d "$@"
}

check_markdown() {
  set -- \
    README.md \
    epaper/weact-2.9-bw/README.md

  run markdownlint-cli2 "$@"
}

check_c() {
  set -- \
    epaper/weact-2.9-bw/main_fast_partial.c \
    epaper/weact-2.9-bw/main_status_screen.c

  write_epaper_stubs
  run gcc -Wall -Wextra -fsyntax-only -I"$tmpdir" "$@"
  run cppcheck --enable=warning,performance,portability --error-exitcode=1 \
    --inline-suppr --suppress=missingIncludeSystem -I"$tmpdir" "$@"
}

need_tool gcc
need_tool shellcheck
need_tool shfmt
need_tool cppcheck
need_tool markdownlint-cli2

run git diff --check
check_shell
check_markdown
check_c

if command -v systemd-analyze >/dev/null 2>&1; then
  if [ -x /home/michal/bin/epd_290_bw_status ] || [ "${CHECK_SYSTEMD_FORCE:-0}" = "1" ]; then
    run systemd-analyze verify systemd/epd-status.service systemd/epd-status.timer
  else
    section "systemd-analyze verify"
    echo "skipped: /home/michal/bin/epd_290_bw_status is not present"
  fi
fi

if command -v gitleaks >/dev/null 2>&1; then
  run gitleaks detect --source . --config .gitleaks.toml --redact
else
  section "gitleaks"
  echo "skipped: gitleaks is not installed"
fi
