#!/usr/bin/env bash
set -euo pipefail

pkill -f 'overlay_controller.py' 2>/dev/null || true
sleep 0.2
nohup "$HOME/.config/sway/overlay_controller.py" >>"${XDG_RUNTIME_DIR:-/tmp}/lumo-overlay.log" 2>&1 &
