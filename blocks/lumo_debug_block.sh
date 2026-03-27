#!/usr/bin/env bash
LOG="${XDG_RUNTIME_DIR:-/tmp}/lumo-overlay.log"

if [ -f "$LOG" ]; then
    line="$(tail -n 1 "$LOG" 2>/dev/null)"
    if [ -n "$line" ]; then
        printf '%s\n' "$line" | cut -c1-140
    else
        printf 'DBG log empty\n'
    fi
else
    printf 'DBG no log yet\n'
fi
