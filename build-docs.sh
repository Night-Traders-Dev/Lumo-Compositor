#!/usr/bin/env bash
set -euo pipefail

OUTDIR="$HOME/sway-docs"
mkdir -p "$OUTDIR"
cd "$OUTDIR"

pages=(
  "1 sway"
  "5 sway"
  "5 sway-input"
  "5 sway-bar"
  "1 swaymsg"
)

i=1
pdfs=()

for entry in "${pages[@]}"; do
  sec="${entry%% *}"
  page="${entry#* }"
  base=$(printf "%02d-%s.%s" "$i" "$page" "$sec")

  man -t "$sec" "$page" > "$base.ps"
  ps2pdf "$base.ps" "$base.pdf"
  pdfs+=("$base.pdf")
  i=$((i+1))
done

pdfunite "${pdfs[@]}" sway-docs-complete.pdf
echo "Created: $OUTDIR/sway-docs-complete.pdf"
