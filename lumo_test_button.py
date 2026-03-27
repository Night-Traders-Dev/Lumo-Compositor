#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import socket

import gi
gi.require_version("Gtk", "3.0")
gi.require_version("GtkLayerShell", "0.1")
from gi.repository import Gtk, GtkLayerShell

SOCKET_PATH = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "lumo-overlay.sock")


def send_open() -> None:
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(0.5)
    sock.connect(SOCKET_PATH)
    sock.sendall((json.dumps({"cmd": "open_launcher"}) + "\n").encode("utf-8"))
    sock.close()


def layer_set_keyboard(window: Gtk.Window, enabled: bool) -> None:
    if hasattr(GtkLayerShell, "set_keyboard_mode") and hasattr(GtkLayerShell, "KeyboardMode"):
        mode = GtkLayerShell.KeyboardMode.ON_DEMAND if enabled else GtkLayerShell.KeyboardMode.NONE
        GtkLayerShell.set_keyboard_mode(window, mode)
    else:
        GtkLayerShell.set_keyboard_interactivity(window, enabled)


class TestButton(Gtk.Window):
    def __init__(self) -> None:
        super().__init__(type=Gtk.WindowType.TOPLEVEL)

        self.set_title("lumo-test-button")
        self.set_decorated(False)
        self.set_resizable(False)
        self.set_skip_taskbar_hint(True)
        self.set_skip_pager_hint(True)
        self.set_app_paintable(True)

        GtkLayerShell.init_for_window(self)
        GtkLayerShell.set_namespace(self, "lumo-test-button")
        GtkLayerShell.set_layer(self, GtkLayerShell.Layer.OVERLAY)
        GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.TOP, True)
        GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.LEFT, True)
        GtkLayerShell.set_margin(self, GtkLayerShell.Edge.TOP, 12)
        GtkLayerShell.set_margin(self, GtkLayerShell.Edge.LEFT, 12)
        GtkLayerShell.set_exclusive_zone(self, 0)
        layer_set_keyboard(self, False)

        button = Gtk.Button(label="Open")
        button.set_size_request(96, 56)
        button.connect("clicked", self.on_clicked)
        self.add(button)

    def on_clicked(self, _button) -> None:
        send_open()


if __name__ == "__main__":
    win = TestButton()
    win.show_all()
    Gtk.main()
