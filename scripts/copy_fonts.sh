#!/bin/bash
# copy_fonts.sh - Copy TTF fonts for the desktop environment
# Usage: ./scripts/copy_fonts.sh (from project root or programs/ directory)

set -e

# Find project root relative to this script's location
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

FONT_DST="$PROJECT_ROOT/programs/bin/fonts"

mkdir -p "$FONT_DST"

FONTS=(
    "programs/gui/fonts/Roboto/static/Roboto-Regular.ttf"
    "programs/gui/fonts/Roboto/static/Roboto-Bold.ttf"
    "programs/gui/fonts/Roboto/static/Roboto-Medium.ttf"
    "programs/gui/fonts/JetBrains_Mono/static/JetBrainsMono-Regular.ttf"
    "programs/gui/fonts/JetBrains_Mono/static/JetBrainsMono-Bold.ttf"
    "programs/gui/fonts/Noto_Serif/static/NotoSerif-Regular.ttf"
    "programs/gui/fonts/Noto_Serif/static/NotoSerif-SemiBold.ttf"
)

copied=0
for font in "${FONTS[@]}"; do
    src="$PROJECT_ROOT/$font"
    name=$(basename "$font")
    dst="$FONT_DST/$name"
    if [ -f "$src" ]; then
        cp "$src" "$dst"
        copied=$((copied + 1))
    else
        echo "copy_fonts: warning: $src not found" >&2
    fi
done

echo "copy_fonts: copied $copied fonts to $FONT_DST"
