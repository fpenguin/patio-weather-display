#!/usr/bin/env sh
set -eu

# Rebuild the two compact Montserrat Black display fonts. The caller supplies
# lv_font_conv and a Montserrat v9.000 checkout to keep this script independent
# of npm and network policy.
if [ "$#" -ne 3 ]; then
    echo "usage: $0 /path/to/lv_font_conv /path/to/Montserrat /project/root" >&2
    exit 2
fi

converter=$1
montserrat=$2
project=$3
source_font="$montserrat/fonts/ttf/Montserrat-Black.ttf"
destination="$project/components/ui/fonts"
glyphs="0x20,0x2B,0x2D,0x30-0x3A,0x41,0x43,0x4D,0x50,0xB0,0x2212"

"$converter" --font "$source_font" --range "$glyphs" --size 112 --bpp 4 \
    --format lvgl --lv-include lvgl.h --lv-font-name patio_font_display_112 \
    -o "$destination/patio_font_display_112.c"
"$converter" --font "$source_font" --range "$glyphs" --size 96 --bpp 4 \
    --format lvgl --lv-include lvgl.h --lv-font-name patio_font_display_96 \
    -o "$destination/patio_font_display_96.c"
