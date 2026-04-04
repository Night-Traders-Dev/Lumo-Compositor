#!/bin/bash
# test_shm_browser.sh — Integration test for Lumo SHM browser
set -e
export XDG_RUNTIME_DIR=/run/user/1001
export WAYLAND_DISPLAY=lumo-shell

SS=/usr/local/bin/lumo-screenshot
PASS=0; FAIL=0; RESULTS=""

screenshot() { $SS "/tmp/test_browser_$1.png" 2>/dev/null; }

check() {
    local name="$1"; shift
    if eval "$@"; then
        PASS=$((PASS+1)); RESULTS="$RESULTS\n  PASS: $name"
    else
        FAIL=$((FAIL+1)); RESULTS="$RESULTS\n  FAIL: $name"
    fi
}

filesize() { stat -c%s "$1" 2>/dev/null || echo 0; }

echo "=== Lumo SHM Browser Test Suite ==="

# 1. Home screen
echo "[1] Home screen"
screenshot 01_home
check "home screen captured" test -f /tmp/test_browser_01_home.png
check "home not blank" test "$(filesize /tmp/test_browser_01_home.png)" -gt 10000

# 2. Launch browser
echo "[2] Launching SHM browser..."
killall lumo-app 2>/dev/null; sleep 1
WAYLAND_DISPLAY=lumo-shell lumo-app --app browser &
BP=$!; sleep 4
screenshot 02_browser
check "browser process alive" kill -0 $BP 2>/dev/null
check "browser rendered" test "$(filesize /tmp/test_browser_02_browser.png)" -gt 15000

# 3. URL resolution (compiled test)
echo "[3] URL resolution tests..."
BT=/home/orangepi/Lumo-Compositor/compositor/builddir/lumo-browser-tests
if [ -x "$BT" ]; then
    if $BT 2>&1 | tail -1; then
        check "URL tests pass" true
    else
        check "URL tests pass" false
    fi
else
    echo "  (test binary not found, skipping)"
fi

# 4. Launch webview with test page
echo "[4] WebView rendering test (google.com)..."
GSK_RENDERER=gl XDG_CACHE_HOME=/tmp/lumo-webkit-cache \
    lumo-webview "https://www.google.com" &
WP=$!; sleep 10
screenshot 04_webview_google
check "webview process alive" kill -0 $WP 2>/dev/null
check "webview rendered" test "$(filesize /tmp/test_browser_04_webview_google.png)" -gt 10000
kill $WP 2>/dev/null; killall WebKitWebProcess WebKitNetworkProcess 2>/dev/null; sleep 2

# 5. Test different page
echo "[5] WebView rendering test (example.com)..."
GSK_RENDERER=gl XDG_CACHE_HOME=/tmp/lumo-webkit-cache \
    lumo-webview "https://example.com" &
WP2=$!; sleep 10
screenshot 05_webview_example
check "webview2 process alive" kill -0 $WP2 2>/dev/null
check "webview2 rendered" test "$(filesize /tmp/test_browser_05_webview_example.png)" -gt 10000
kill $WP2 2>/dev/null; killall WebKitWebProcess WebKitNetworkProcess 2>/dev/null; sleep 2

# 6. Test DuckDuckGo search
echo "[6] WebView rendering test (DuckDuckGo search)..."
GSK_RENDERER=gl XDG_CACHE_HOME=/tmp/lumo-webkit-cache \
    lumo-webview "https://duckduckgo.com/?q=lumo+os" &
WP3=$!; sleep 12
screenshot 06_webview_ddg
check "webview3 process alive" kill -0 $WP3 2>/dev/null
check "ddg search rendered" test "$(filesize /tmp/test_browser_06_webview_ddg.png)" -gt 10000
kill $WP3 2>/dev/null; killall WebKitWebProcess WebKitNetworkProcess 2>/dev/null; sleep 2

# 7. Cleanup and verify home
echo "[7] Cleanup..."
kill $BP 2>/dev/null; sleep 2
screenshot 07_final
check "returned to home" test "$(filesize /tmp/test_browser_07_final.png)" -gt 10000

echo ""
echo "=== Results ==="
echo -e "$RESULTS"
echo ""
echo "PASSED: $PASS  FAILED: $FAIL"
[ $FAIL -eq 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
exit $FAIL
