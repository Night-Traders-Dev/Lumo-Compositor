#!/bin/bash
# Lumo Compositor Live Device Tests
# Runs on the OrangePi while the compositor is active.
# Injects touch events, takes screenshots, checks logs.
#
# Usage: ./run_live_tests.sh [test_name]
# Requires: root (for /dev/input), lumo-screenshot

set -e

TOUCH_DEV="/dev/input/event2"
SCREENSHOT_DIR="/tmp/lumo-tests"
LOG_FILE="$SCREENSHOT_DIR/test.log"
XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
export XDG_RUNTIME_DIR
PASS=0
FAIL=0

mkdir -p "$SCREENSHOT_DIR"
echo "=== Lumo Live Tests $(date) ===" > "$LOG_FILE"

log() { echo "[$(date +%H:%M:%S)] $*" | tee -a "$LOG_FILE"; }
screenshot() {
    local name="$1"
    local outfile="$SCREENSHOT_DIR/${name}.png"
    # run as the compositor's user with correct runtime dir
    su -c "XDG_RUNTIME_DIR=/run/user/1001 /usr/local/bin/lumo-screenshot --output $outfile" orangepi 2>/dev/null || true
    if [ -f "$outfile" ]; then
        log "  screenshot: ${name}.png ($(stat -c%s "$outfile") bytes)"
    else
        log "  screenshot: ${name}.png (FAILED)"
    fi
}

# Inject a single tap at normalized coordinates (0.0-1.0)
inject_tap() {
    local nx="$1" ny="$2" duration="${3:-100}"

    python3 - "$TOUCH_DEV" "$nx" "$ny" "$duration" << 'PYEOF'
import struct, os, time, sys
dev, nx, ny, dur = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), int(sys.argv[4])
# Standard HID multitouch: ABS_MT coords 0-32767 for this device
ax = int(nx * 32767)
ay = int(ny * 32767)
EV_ABS, EV_KEY, EV_SYN = 3, 1, 0
ABS_X, ABS_Y = 0, 1
ABS_MT_SLOT, ABS_MT_TRACKING_ID = 0x2f, 0x39
ABS_MT_POSITION_X, ABS_MT_POSITION_Y = 0x35, 0x36
BTN_TOUCH, SYN_REPORT = 0x14a, 0
fd = os.open(dev, os.O_WRONLY | os.O_NONBLOCK)
def ev(t,c,v):
    # timeval is 2 longs (sec, usec) + type(H) + code(H) + value(i)
    ts = time.clock_gettime(time.CLOCK_REALTIME)
    sec = int(ts)
    usec = int((ts - sec) * 1000000)
    os.write(fd, struct.pack('@llHHi', sec, usec, t, c, v))
ev(EV_ABS, ABS_MT_SLOT, 0)
ev(EV_ABS, ABS_MT_TRACKING_ID, 100)
ev(EV_ABS, ABS_MT_POSITION_X, ax)
ev(EV_ABS, ABS_MT_POSITION_Y, ay)
ev(EV_ABS, ABS_X, ax)
ev(EV_ABS, ABS_Y, ay)
ev(EV_KEY, BTN_TOUCH, 1)
ev(EV_SYN, SYN_REPORT, 0)
time.sleep(dur / 1000.0)
ev(EV_ABS, ABS_MT_SLOT, 0)
ev(EV_ABS, ABS_MT_TRACKING_ID, -1)
ev(EV_KEY, BTN_TOUCH, 0)
ev(EV_SYN, SYN_REPORT, 0)
os.close(fd)
PYEOF
}

check_log() {
    local pattern="$1" desc="$2"
    if journalctl --no-pager -n 200 2>/dev/null | grep -qE "$pattern"; then
        log "  PASS: $desc"
        PASS=$((PASS + 1))
    else
        log "  FAIL: $desc (pattern: $pattern)"
        FAIL=$((FAIL + 1))
    fi
}

check_process() {
    local name="$1" desc="$2"
    if pgrep -f "$name" >/dev/null 2>&1; then
        log "  PASS: $desc"
        PASS=$((PASS + 1))
    else
        log "  FAIL: $desc"
        FAIL=$((FAIL + 1))
    fi
}

# ============================================================
# TEST 1: Compositor running
# ============================================================
test_compositor_running() {
    log "TEST: compositor processes"
    check_process "lumo-compositor" "compositor running"
    check_process "lumo-shell --mode launcher" "launcher shell running"
    check_process "lumo-shell --mode osk" "OSK shell running"
    check_process "lumo-shell --mode status" "status bar running"
    check_process "lumo-shell --mode background" "background running"
    check_process "lumo-shell --mode gesture" "gesture handle running"
    screenshot "01_compositor_running"
}

# ============================================================
# TEST 2: Weather fetch
# ============================================================
test_weather() {
    log "TEST: weather system"
    sleep 4  # wait for initial fetch
    check_log "weather:.*code=" "weather data received"
    screenshot "02_weather"
}

# ============================================================
# TEST 3: Open app drawer (bottom edge swipe)
# ============================================================
test_open_drawer() {
    log "TEST: open app drawer"
    # swipe up from bottom edge
    inject_tap 0.5 0.98 150
    sleep 1
    check_log "launcher visible" "launcher toggled"
    screenshot "03_drawer_open"
}

# ============================================================
# TEST 4: Tap a launcher tile (Terminal - tile index 1)
# ============================================================
test_launch_app() {
    log "TEST: launch terminal app"
    # tap in the area where tile 1 should be (center-ish of drawer)
    inject_tap 0.5 0.4 100
    sleep 2
    check_log "launched.*lumo-app" "app process launched"
    check_process "lumo-app" "app process running"
    screenshot "04_app_launched"
}

# ============================================================
# TEST 5: Quick settings panel (top-right swipe)
# ============================================================
test_quick_settings() {
    log "TEST: quick settings panel"
    # close any open app first
    inject_tap 0.5 0.98 150
    sleep 1
    # tap top-right edge
    inject_tap 0.95 0.01 100
    sleep 1
    check_log "quick_settings visible" "quick settings opened"
    screenshot "05_quick_settings"
    # dismiss
    inject_tap 0.3 0.5 100
    sleep 1
}

# ============================================================
# TEST 6: Time/date panel (top-left swipe)
# ============================================================
test_time_panel() {
    log "TEST: time/date panel"
    inject_tap 0.05 0.01 100
    sleep 1
    check_log "time_panel" "time panel toggled"
    screenshot "06_time_panel"
    # dismiss
    inject_tap 0.8 0.5 100
    sleep 1
}

# ============================================================
# TEST 7: Bottom swipe to close app
# ============================================================
test_close_app() {
    log "TEST: close app via bottom swipe"
    # open drawer, launch an app
    inject_tap 0.5 0.98 150
    sleep 1
    inject_tap 0.5 0.5 100
    sleep 2
    screenshot "07a_app_open"
    # swipe up from bottom to close
    inject_tap 0.5 0.99 150
    sleep 1
    check_log "closed focused app\|launcher visible" "app closed or launcher toggled"
    screenshot "07b_after_close"
}

# ============================================================
# TEST 8: Screenshot tool works
# ============================================================
test_screenshot_tool() {
    log "TEST: screenshot tool"
    if su -c "XDG_RUNTIME_DIR=/run/user/1001 /usr/local/bin/lumo-screenshot --output $SCREENSHOT_DIR/08_screenshot_test.png" orangepi 2>/dev/null; then
        if [ -f "$SCREENSHOT_DIR/08_screenshot_test.png" ]; then
            local sz=$(stat -c%s "$SCREENSHOT_DIR/08_screenshot_test.png")
            if [ "$sz" -gt 1000 ]; then
                log "  PASS: screenshot tool ($sz bytes)"
                PASS=$((PASS + 1))
            else
                log "  FAIL: screenshot too small ($sz bytes)"
                FAIL=$((FAIL + 1))
            fi
        else
            log "  FAIL: screenshot file not created"
            FAIL=$((FAIL + 1))
        fi
    else
        log "  FAIL: screenshot command failed"
        FAIL=$((FAIL + 1))
    fi
}

# ============================================================
# RUN
# ============================================================
if [ -n "$1" ]; then
    "test_$1"
else
    test_compositor_running
    test_weather
    test_screenshot_tool
    test_open_drawer
    sleep 1
    test_launch_app
    sleep 1
    test_quick_settings
    test_time_panel
    test_close_app
fi

echo ""
log "=== RESULTS: $PASS passed, $FAIL failed ==="
echo ""
echo "Screenshots in: $SCREENSHOT_DIR/"
echo "Log: $LOG_FILE"
exit $FAIL
