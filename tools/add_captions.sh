#!/bin/sh
# Burns captions into a video.
#   usage: ./add_captions.sh input.mov captions.txt output.mp4
# captions.txt: one caption per line: <start_sec> <end_sec> <text>
set -e
IN="$1"; CAPS="$2"; OUT="$3"
[ -f "$IN" ] && [ -f "$CAPS" ] && [ -n "$OUT" ] || { echo "usage: $0 input captions.txt output.mp4"; exit 1; }

# Font: macOS first, then common Linux paths.
for f in /System/Library/Fonts/Supplemental/Arial.ttf /System/Library/Fonts/Helvetica.ttc \
         /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf; do
  [ -f "$f" ] && FONT="$f" && break
done
[ -n "$FONT" ] || { echo "no font found"; exit 1; }

TMP=$(mktemp -d)
FILTER=""
i=0
grep -v '^#' "$CAPS" | grep -v '^[[:space:]]*$' | while read -r START END TEXT; do
  i=$((i + 1))
  # Each caption goes into its own file (textfile=), so no escaping of : % , etc.
  printf '%s' "$TEXT" > "$TMP/cap$i.txt"
  echo "drawtext=expansion=none:fontfile='$FONT':textfile='$TMP/cap$i.txt':fontsize=h/22:fontcolor=white:box=1:boxcolor=black@0.6:boxborderw=18:x=(w-text_w)/2:y=h-text_h-h/12:enable='between(t,$START,$END)'"
done > "$TMP/filters"
FILTER=$(paste -sd, "$TMP/filters")

ffmpeg -y -i "$IN" -vf "scale=-2:min(1080\,ih),$FILTER" -c:v libx264 -crf 26 -preset medium -r 30 -pix_fmt yuv420p -an "$OUT"
rm -rf "$TMP"
ls -lh "$OUT"
