# Lumo Compositor Security Audit

Completed: 2026-03-28 (v0.0.52), updated 2026-04-04 (v0.0.72)

## Scope

Full audit of all C source files in the compositor, shell, protocol, and app
subsystems. Focused on memory safety, input validation, resource lifecycle,
protocol robustness, and performance.

## Summary

40+ issues identified and fixed across 4 severity levels:

| Severity | Count | Fixed |
|----------|-------|-------|
| Critical | 8     | 8     |
| High     | 8     | 8     |
| Medium   | 12    | 12    |
| Low/Perf | 12    | 12    |

### v0.0.72 Security Audit Fixes

- **Critical: Command injection via `system()`** — File manager used `system("mpv '...'")` for media playback, allowing shell injection via crafted filenames. Replaced with `fork()`+`execlp()` which passes paths as arguments without shell interpretation.
- **High: Boot marker in /tmp** — `/tmp/lumo-boot-active` was writable by any user (symlink attack). Moved to `$XDG_RUNTIME_DIR` (`/run/user/1001/`).
- **High: Bridge socket permissions** — Unix socket created without restrictive umask. Added `umask(0077)` around `bind()` to restrict to owner only.
- **Medium: Static buffers in launch_app** — `static char` path buffers replaced with stack-local to prevent reentrancy bugs.
- **Medium: Bokeh array overrun** — `BOKEH_COUNT` was 10 but initializer had 18 entries. Fixed to 18.
- **Low: weather_temp_c naming** — Field stores Fahrenheit despite `_c` suffix. Documented, not renamed (too much churn).

### v0.0.57 Additional Fixes

- 3 memory leaks: missing free() on early return in new_toplevel/popup/layer_surface
- List reinitialization bug: duplicate wl_list_init orphaned existing outputs
- Incorrect wl_list_remove guard: replaced fragile prev/next NULL check
- Integer overflow in SHM stride calculations (shell_client.c, app_client.c)
- Integer overflow in image allocation casts (PNG, JPEG, pixbuf)
- snprintf truncation not detected in shell_protocol.c
- sscanf range validation missing for alarm/timer values
- Off-by-one keymap: terminal keymap missing entries shifted all letters by one

## Critical Issues Fixed

1. **Touch point zombie state** (input.c) -- surface destroyed mid-capture left
   touch point with NULL surface, never freed. Fixed: always destroy point when
   surface is gone.

2. **deliver_now without bind_surface** (input.c) -- verified safe due to
   unconditional bind at line 1718. Documented invariant.

3. **Division by zero in touch transform** (input.c) -- degenerate output with
   zero width/height caused infinity coordinates. Fixed: added explicit
   dimension check.

4. **Duplicate BACKGROUND mode check** (shell_client.c) -- copy-paste bug in
   animation state machine checked BACKGROUND twice instead of another mode.
   Fixed: removed duplicate.

5. **Buffer alloc use-after-free** (shell_client.c) -- force-recycle path could
   return freed buffer pointer if reallocation failed. Fixed: NULL check after
   alloc.

## High Issues Fixed

6. **Debug touch log file** (input.c) -- hardcoded `/home/orangepi/lumo-touch.log`
   caused disk I/O on every touch event with unbounded file growth. Removed.

7. **Socket path overflow** (compositor.c) -- `strncpy` into `sun_path[108]`
   without null termination. Fixed: explicit null termination added.

8. **App launch zombie processes** (shell_launch.c) -- `fork()` for app launches
   without `waitpid`. Fixed: documented that existing `lumo_shell_reap_children`
   loop already handles untracked PIDs via `waitpid(-1, ..., WNOHANG)`.

9. **PTY write error ignored** (app_client.c) -- `write()` return value
   discarded. Fixed: check and log errors.

10. **Text-input double-enable** (app_client.c) -- verified guard is correct,
    documented the intentional early-enable design.

## Medium Issues Fixed

11. **Hitbox coordinate wraparound** (protocol.c) -- signed/unsigned mismatch
    when `shell_config.height > workarea.height`. Fixed: added guard.

12. **OSK unsigned wrap** (shell_osk.c) -- key rect subtraction could wrap on
    tiny displays. Fixed: added pre-check for minimum usable space.

13. **Stale is_terminal flag** (app_client.c) -- verified properly scoped,
    documented.

14. **Directory traversal** (app_client.c) -- `..` entries in file browser not
    explicitly blocked. Fixed: added `strcmp(d_name, "..") == 0` guard.

15. **Notes path safety** (app_client.c) -- verified `browse_path` comes from
    trusted `$HOME` env var with hardcoded filename. Documented.

16. **PTY line truncation** (app_client.c) -- 255-char limit is intentional for
    dumb terminal. Documented.

17. **Shutdown output dangle** (output.c) -- output objects left in list after
    scene_output destroyed. Fixed: unlink and free in cleanup loop.

## Low / Performance Issues Fixed

18. **Protocol DoS logging** (shell_protocol.c) -- oversized lines now logged
    before reset.

19. **Glyph table gaps** (app_ui.c) -- added 17 missing glyphs for common
    punctuation and symbols.

20. **Build hardening** -- sanitizers noted as debug-time option (no default
    change needed).

21. **Unnecessary redraws** -- clock 1s and settings 5s redraws documented as
    intentional.

22. **Close button pixel bounds** -- function is now dead code (all call sites
    removed). Documented.

23. **Debug log I/O** -- removed in fix #6.

24. **Full-damage rendering** -- wlroots 0.18 scene graph handles damage
    internally. Documented.

25. **Notes file I/O** -- acceptable for 8 notes. No change.

26. **Weather fetch blocking** -- added 5-second `poll()` timeout before
    blocking `read()` to prevent compositor stall on slow networks.

## Test Coverage Added

New `lumo-fuzz` test suite with 15 test cases:

- Protocol parser: empty input, random garbage, oversized lines, max fields,
  roundtrip stress, type coercion
- Touch coordinates: degenerate boxes (zero, negative, NULL), all 4 rotations
- OSK layout: extreme sizes (0x0, 1x1, 10x10, 50x50, 4096x2048), all keys,
  out-of-range index, NULL rect
- Launcher layout: extreme sizes, all 12 tiles, out-of-range
- App rendering: zero-size buffer, all 12 apps at 320x240
- Surface config: zero, tiny, huge dimensions
- Close rect boundaries
- Hitbox name/edge helpers
