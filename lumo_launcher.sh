#!/usr/bin/env bash
set -eu

pkill -f '/.config/sway/lumo_gesture.py' 2>/dev/null || true
sleep 0.15
nohup "$HOME/.config/sway/lumo_gesture.py" >/tmp/lumo_gesture.log 2>&1 &
