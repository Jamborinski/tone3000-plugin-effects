#!/usr/bin/env bash
# tile-ocr.sh -- read tiny tile button text without a vision model.
#
# Shoots a TileShot tile (or takes a PNG), crops a button region by design
# coordinates, and reads it with Tesseract (or prints ASCII art as fallback).
#
#   ./script/tile-ocr.sh comp-shift                 # shoot + crop type button (dark)
#   ./script/tile-ocr.sh comp-shift --ocr           # shoot + crop (OCR palette)
#   ./script/tile-ocr.sh comp                       # shoot + crop
#   ./script/tile-ocr.sh --png /tmp/shot.png --region 166,44,44,26
#   ./script/tile-ocr.sh comp-shift --region 48,44,120,26   # mode combo instead
#   ./script/tile-ocr.sh comp-shift --no-tesseract  # force ASCII art output
#
# --region is DESIGN pixels (the 224-wide layout; --dump prints these). The
# shot is 2x (448px), the script scales it. Defaults:
#   comp tiles: type button @166,44 44x26
#   everything: --region required
# Env: T3K_OCR_THRESHOLD (default 140, 0..255 brightness cut for ASCII mode),
#       T3K_OCR_SCALE (default 2, upscale factor for the crop).
set -euo pipefail
cd "$(dirname "$0")/.."

TILE=""
PNG=""
REGION=""
OCR_MODE=false
NO_TESS=false

while [ $# -gt 0 ]; do
  case "$1" in
    --png)       PNG="$2"; shift 2 ;;
    --region)    REGION="$2"; shift 2 ;;
    --ocr)       OCR_MODE=true; shift ;;
    --no-tesseract) NO_TESS=true; shift ;;
    -h|--help)   sed -n '2,22p' "$0"; exit 0 ;;
    *)           TILE="$1"; shift ;;
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
  if [ "$OCR_MODE" = true ]; then
    xvfb-run -a "$TS" --tile "$TILE" --ocr --out "$PNG" >/dev/null
    echo "# shot: $TILE --ocr -> $PNG (high-contrast)"
  else
    xvfb-run -a "$TS" --tile "$TILE" --out "$PNG" >/dev/null
    echo "# shot: $TILE -> $PNG"
  fi
fi
[ -f "$PNG" ] || { echo "no PNG: pass a tile or --png" >&2; exit 1; }
[ -n "$REGION" ] || { echo "--region x,y,w,h required" >&2; exit 2; }

# Crop the region (design coords * 2 for 2x shots) with padding, upscale.
CROP_PNG=$(mktemp /tmp/tile-ocr-crop-XXXX.png)
python3 - "$PNG" "$REGION" "$CROP_PNG" <<'PYEOF'
import sys, os
from PIL import Image
png, region, out = sys.argv[1], sys.argv[2], sys.argv[3]
x, y, w, h = (int(v) for v in region.split(','))
scale = int(os.environ.get('T3K_OCR_SCALE', '2'))
im = Image.open(png).convert('L')
# shots are 2x design pixels
pad = 6
box = (max(0, int(2*(x-pad))), max(0, int(2*(y-pad))),
       int(2*(x+w+pad)), int(2*(y+h+pad)))
crop = im.crop(box)
crop = crop.resize((crop.width * scale, crop.height * scale), Image.LANCZOS)
# Binarize for tesseract (pure PIL): threshold at the crop's mid-brightness,
# then ensure the result is dark text on a mostly-white background (invert
# if the crop is overall dark, e.g. a dark-mode shot).
mid = (crop.getextrema()[0] + crop.getextrema()[1]) // 2
crop = crop.point(lambda p: 0 if p < mid else 255)
hist = crop.histogram()
if hist[0] > hist[255]:  # black majority -> dark bg, flip to text-on-white
    crop = crop.point(lambda p: 255 - p)
# White margin
margin = 20
big = Image.new('L', (crop.width + 2*margin, crop.height + 2*margin), 255)
big.paste(crop, (margin, margin))
big.save(out)
PYEOF

if [ "$NO_TESS" = false ] && command -v tesseract >/dev/null 2>&1; then
  echo "# Tesseract output:"
  for psm in 7 13 10; do
    result=$(tesseract "$CROP_PNG" - --psm $psm -c tessedit_char_whitelist=ABCDEFGHIJKLMNOPQRSTUVWXYZ 2>/dev/null | tr -d '\n')
    if [ -n "$result" ]; then
      echo "  psm$psm: $result"
    fi
  done
  # Also try the confidence TSV
  echo "# Confidence:"
  tesseract "$CROP_PNG" - --psm 13 tsv 2>/dev/null | awk -F'\t' 'NR>1 && $12!="" {print "  "$12, "conf="$11}' || true
else
  echo "# ASCII art (no tesseract):"
  python3 - "$CROP_PNG" <<'EOF'
import sys, os
from PIL import Image
im = Image.open(sys.argv[1])
band = im.split()[0]  # luminance, no deprecation
thr = int(os.environ.get('T3K_OCR_THRESHOLD', '140'))
px = band.load()
for j in range(im.size[1]):
    row = ''.join('#' if px[i, j] < thr else ' ' for i in range(im.size[0]))
    if row.strip():
        print(row.rstrip())
EOF
fi

rm -f "$CROP_PNG"
