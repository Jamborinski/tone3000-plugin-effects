#!/usr/bin/env bash
# tile-ocr.sh -- read tiny tile button text without a vision model.
#
# Shoots a TileShot tile (or takes a PNG), crops a button region by design
# coordinates, and prints it as ASCII art so a text model (or you) can read
# 13px labels like the compressor type button (VCA/TUB/OPT/VMU/FET).
#
#   ./script/tile-ocr.sh comp-shift                 # shoot + crop type button
#   ./script/tile-ocr.sh comp                       # shoot + crop
#   ./script/tile-ocr.sh --png /tmp/shot.png --region 166,44,44,26
#   ./script/tile-ocr.sh comp-shift --region 48,44,120,26   # mode combo instead
#
# --region is DESIGN pixels (the 224-wide layout; --dump prints these). The
# shot is 2x (448px), the script scales it. Defaults:
#   comp tiles: type button @166,44 44x26
#   everything: --region required
# Env: T3K_OCR_THRESHOLD (default 140, 0..255 brightness cut), T3K_OCR_SCALE (default 2).
set -euo pipefail
cd "$(dirname "$0")/.."

TILE=""
PNG=""
REGION=""

while [ $# -gt 0 ]; do
  case "$1" in
    --png)    PNG="$2"; shift 2 ;;
    --region) REGION="$2"; shift 2 ;;
    -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
    *)        TILE="$1"; shift ;;
  esac
done

export LD_LIBRARY_PATH="$HOME/buildkit/stage/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
TILES=(comp comp-shift)
for t in "${TILES[@]}"; do [ "$TILE" = "$t" ] && { [ -z "$REGION" ] && REGION="166,44,44,26"; break; }; done
if [ -z "$PNG" ] && [ -z "$REGION" ] && [ -n "$TILE" ]; then
  echo "--region required for non-comp tiles (get bounds from: TileShot --dump <tile>)" >&2
  exit 2
fi

if [ -n "$TILE" ]; then
  TS=build-ui/TileShot_artefacts/Release/TileShot
  [ -x "$TS" ] || { echo "TileShot not built: cmake --build build-ui --target TileShot" >&2; exit 1; }
  PNG=$(mktemp /tmp/tile-ocr-XXXX.png)
  xvfb-run -a "$TS" --tile "$TILE" --out "$PNG" >/dev/null
  echo "# shot: $TILE -> $PNG"
fi
[ -f "$PNG" ] || { echo "no PNG: pass a tile or --png" >&2; exit 1; }
[ -n "$REGION" ] || { echo "--region x,y,w,h required" >&2; exit 2; }

python3 - "$PNG" "$REGION" <<'EOF'
import sys
from PIL import Image
png, region = sys.argv[1], sys.argv[2]
x, y, w, h = (int(v) for v in region.split(','))
thr = int(__import__('os').environ.get('T3K_OCR_THRESHOLD', '140'))
scale = int(__import__('os').environ.get('T3K_OCR_SCALE', '2'))
im = Image.open(png).convert('L')
# shots are 2x design pixels
pad = 4
box = tuple(int(2 * v) for v in (x - pad, y - pad, x + w + pad, y + h + pad))
c = im.crop(box).resize((int(box[2] - box[0]) * scale, int(box[3] - box[1]) * scale), Image.NEAREST)
px = c.load()
for j in range(c.size[1]):
    row = ''.join('#' if px[i, j] > thr else ' ' for i in range(c.size[0]))
    print(row.rstrip())
EOF
