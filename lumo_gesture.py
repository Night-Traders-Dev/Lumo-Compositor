#!/usr/bin/env python3
"""Bottom-edge gesture surface for Lumo on Sway."""

from __future__ import annotations

import json
import os
import socket
import time

import gi

gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
gi.require_version("GtkLayerShell", "0.1")
from gi.repository import Gdk, Gtk, GtkLayerShell

EDGE_HEIGHT = 220
DRAG_THRESHOLD_PX = 60
SOCKET_PATH = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "lumo-overlay.sock")
LOG_PATH = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "lumo-overlay.log")


def log(msg: str) -> None:
    line = f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] gesture: {msg}"
    print(line, flush=True)
    try:
        with open(LOG_PATH, "a", encoding="utf-8") as f:
            f.write(line + "\n")
    except Exception:
        pass


def layer_set_keyboard(window: Gtk.Window, enabled: bool) -> None:
    if hasattr(GtkLayerShell, "set_keyboard_mode") and hasattr(GtkLayerShell, "KeyboardMode"):
        mode = GtkLayerShell.KeyboardMode.ON_DEMAND if enabled else GtkLayerShell.KeyboardMode.NONE
        GtkLayerShell.set_keyboard_mode(window, mode)
    else:
        GtkLayerShell.set_keyboard_interactivity(window, enabled)


def send_controller(message: dict) -> bool:
    try:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.settimeout(0.5)
        sock.connect(SOCKET_PATH)
        sock.sendall((json.dumps(message) + "\n").encode("utf-8"))
        sock.close()
        log(f"sent ipc: {message}")
        return True
    except Exception as exc:
        log(f"ipc error: {exc}")
        return False


class GestureArea(Gtk.EventBox):
    def __init__(self) -> None:
        super().__init__()

        self.set_visible_window(False)
        self.set_above_child(True)
        self.add_events(
            Gdk.EventMask.BUTTON_PRESS_MASK
            | Gdk.EventMask.BUTTON_RELEASE_MASK
            | Gdk.EventMask.POINTER_MOTION_MASK
            | Gdk.EventMask.TOUCH_MASK
        )

        self.dragging = False
        self.triggered = False
        self.start_y = None

        self.connect("button-press-event", self.on_button_press)
        self.connect("motion-notify-event", self.on_motion)
        self.connect("button-release-event", self.on_button_release)
        self.connect("touch-event", self.on_touch)

    def begin_drag(self, y: float, source: str) -> None:
        self.dragging = True
        self.triggered = False
        self.start_y = y
        log(f"{source} begin y={y:.1f}")

    def update_drag(self, y: float, source: str) -> None:
        if not self.dragging or self.start_y is None:
            return
        dy = y - self.start_y
        log(f"{source} update y={y:.1f} dy={dy:.1f}")
        if not self.triggered and dy <= -DRAG_THRESHOLD_PX:
            self.triggered = True
            send_controller({"cmd": "open_launcher"})

    def end_drag(self, y: float | None, source: str) -> None:
        if self.start_y is not None and y is not None:
            dy = y - self.start_y
            log(f"{source} end y={y:.1f} dy={dy:.1f}")
        else:
            log(f"{source} end")
        self.dragging = False
        self.triggered = False
        self.start_y = None

    def on_button_press(self, _widget, event):
        self.begin_drag(event.y, "button")
        return True

    def on_motion(self, _widget, event):
        self.update_drag(event.y, "motion")
        return True

    def on_button_release(self, _widget, event):
        self.end_drag(event.y, "button")
        return True

    def on_touch(self, _widget, event):
        etype = event.type
        y = getattr(event, "y", None)

        if etype == Gdk.EventType.TOUCH_BEGIN and y is not None:
            self.begin_drag(y, "touch")
            return True

        if etype == Gdk.EventType.TOUCH_UPDATE and y is not None:
            self.update_drag(y, "touch")
            return True

        if etype in (Gdk.EventType.TOUCH_END, Gdk.EventType.TOUCH_CANCEL):
            self.end_drag(y, "touch")
            return True

        return False


class GestureWindow(Gtk.Window):
    def __init__(self) -> None:
        super().__init__(type=Gtk.WindowType.TOPLEVEL)

        if not GtkLayerShell.is_supported():
            log("GtkLayerShell is not supported in this session")
            raise SystemExit(1)

        self.set_title("lumo-gesture")
        self.set_decorated(False)
        self.set_accept_focus(False)
        self.set_focus_on_map(False)
        self.set_app_paintable(True)
        self.set_resizable(False)
        self.set_skip_taskbar_hint(True)
        self.set_skip_pager_hint(True)
        self.stick()

        screen = self.get_screen()
        if screen is not None:
            visual = screen.get_rgba_visual()
            if visual is not None:
                self.set_visual(visual)

        GtkLayerShell.init_for_window(self)
        GtkLayerShell.set_namespace(self, "lumo-gesture")
        GtkLayerShell.set_layer(self, GtkLayerShell.Layer.OVERLAY)
        GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.LEFT, True)
        GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.RIGHT, True)
        GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.BOTTOM, True)
        GtkLayerShell.set_exclusive_zone(self, 0)
        layer_set_keyboard(self, False)

        self.set_size_request(-1, EDGE_HEIGHT)

        area = GestureArea()
        self.add(area)

        log(f"gesture window started edge_height={EDGE_HEIGHT} threshold={DRAG_THRESHOLD_PX}")


if __name__ == "__main__":
    win = GestureWindow()
    win.show_all()
    Gtk.main()
