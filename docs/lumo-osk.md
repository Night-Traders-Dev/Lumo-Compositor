# Lumo On-Screen Keyboard

## Overview

The OSK is a shell client (`lumo-shell --mode osk`) rendering as a
layer-shell surface on the OVERLAY layer. It occupies ~40% of screen height,
anchored to the bottom edge.

## Pages

The keyboard has two pages, switchable via a toggle key:

### QWERTY Page (default)

```text
 q  w  e  r  t  y  u  i  o  p
 a  s  d  f  g  h  j  k  l  <-
 ^  z  x  c  v  b  n  m  123
 .       SPACE       ENTER   v
```

### Symbols Page

```text
 1  2  3  4  5  6  7  8  9  0
 @  #  $  %  &  -  +  (  )  <-
 ^  !  ?  ,  ;  :  '  /  ABC
 .       SPACE       ENTER   v
```

## Special Keys

| Key | Label | Action |
|-----|-------|--------|
| Shift | `^` | Toggle uppercase. One-shot: auto-clears after one letter. Highlights orange when active. |
| Backspace | `<-` | Delete one character (sends `\b` or `delete_surrounding_text`) |
| Page toggle | `123` / `ABC` | Switch between QWERTY and symbols pages |
| Close | `v` | Hide the keyboard (`lumo_protocol_set_keyboard_visible(false)`) |
| Space | `SPACE` | Insert space character |
| Enter | `ENTER` | Insert newline / submit |

## Shift Behavior

- Tap shift: labels switch from lowercase to uppercase, shift key highlights orange
- Type a letter: the letter is sent uppercase, shift auto-clears back to lowercase
- Non-letter keys (space, enter, punctuation) do not consume shift
- Shift state is tracked in `compositor->osk_shift_active` and broadcast to the shell client

## Key Routing Priority

When a key is pressed, the compositor handles it in this order:

1. **Shift** (empty text `""`) → toggle shift state
2. **Close** (`\x1b`) → hide keyboard
3. **Page toggle** (`\x01`) → switch QWERTY/symbols
4. **Backspace** (`\b`) → delete character
5. **Text-input-v3** → send commit_string to focused text input
6. **Virtual keyboard fallback** → send Linux keycode via wlr_seat (for apps without text-input-v3)
7. **Search bar** → route to launcher search when drawer is visible

## Virtual Keyboard Fallback

When text-input-v3 is not available, the compositor maps characters to Linux
keycodes and sends them as `wl_keyboard` key events. For uppercase letters
and shifted symbols, LEFT_SHIFT (keycode 42) is pressed before and released
after the character keycode.

The terminal app tracks shift state from these key events and maintains a
full shifted keymap (`!@#$%^&*()_+` etc.) for proper symbol input.

## Hitbox Priority

The OSK hitbox is registered after the launcher hitbox in the compositor's
hitbox list, giving it higher priority in the overlap region. This prevents
touches on OSK keys from accidentally triggering launcher tiles behind the
keyboard.

## Page Toggle Broadcast

The current OSK page (QWERTY or symbols) is broadcast to the shell client
via the `osk_page` field in the state protocol frame. When the user taps
the `123`/`ABC` key, the compositor updates `osk_page` and triggers a
state broadcast so the shell renders the correct key labels immediately.

## Key Press Visual Feedback

In unified rendering mode, key press feedback is handled by calling
`lumo_shell_client_redraw_unified()` when the active target (pressed key)
changes. This ensures the pressed-key highlight renders without waiting
for the next animation tick.

## State Reset

When the keyboard is hidden:

- Shift state resets to off
- Page resets to QWERTY
- `osk_shift_active` cleared in compositor
- `osk_page` reset to 0 (QWERTY)
- State broadcast to shell clients

## Implementation Files

- `src/shell/shell_osk.c` — key layout, page arrays, rect calculation
- `src/shell/shell_render.c` — key rendering with shift/page visual feedback
- `src/shell/shell_launch.c` — key commit routing, shift/close/page handling
- `src/shell/shell_input.c` — touch hit detection on OSK surface
- `src/shell/shell_ui.c` — target resolution for OSK keys
- `src/protocol/protocol.c` — keyboard visibility state machine, hitbox registration
