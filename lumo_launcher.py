#!/usr/bin/env python3
"""Layer-shell launcher for Lumo controlled by overlay_controller.py."""

from __future__ import annotations

import configparser
import json
import os
import shlex
import socket
import subprocess

import gi

gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
gi.require_version("GdkPixbuf", "2.0")
gi.require_version("GtkLayerShell", "0.1")

from gi.repository import Gdk, GdkPixbuf, Gtk, GtkLayerShell

APP_DIRS = [
    "/usr/share/applications",
    os.path.expanduser("~/.local/share/applications"),
]

ENV_CONFIG_PATH = os.path.expanduser("~/.config/sway/launcher_env.json")
SOCKET_PATH = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "lumo-overlay.sock")

ICON_SIZE = 72
BUTTON_WIDTH = 170
BUTTON_HEIGHT = 150
GRID_SPACING = 18
WINDOW_PADDING = 24
TOP_MARGIN = 36
MIN_COLS = 3


def layer_set_keyboard(window: Gtk.Window, enabled: bool) -> None:
    if hasattr(GtkLayerShell, "set_keyboard_mode") and hasattr(GtkLayerShell, "KeyboardMode"):
        mode = GtkLayerShell.KeyboardMode.ON_DEMAND if enabled else GtkLayerShell.KeyboardMode.NONE
        GtkLayerShell.set_keyboard_mode(window, mode)
    else:
        GtkLayerShell.set_keyboard_interactivity(window, enabled)


def send_controller(message: dict) -> None:
    try:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.settimeout(0.25)
        sock.connect(SOCKET_PATH)
        sock.sendall((json.dumps(message) + "\n").encode("utf-8"))
        sock.close()
    except Exception:
        pass


def load_env_config() -> dict:
    if not os.path.exists(ENV_CONFIG_PATH):
        return {}
    try:
        with open(ENV_CONFIG_PATH, "r", encoding="utf-8") as handle:
            data = json.load(handle)
        return data if isinstance(data, dict) else {}
    except Exception:
        return {}


ENV_OVERRIDES = load_env_config()


def clean_exec(exec_line: str) -> list[str]:
    try:
        parts = shlex.split(exec_line)
    except Exception:
        parts = exec_line.split()

    cleaned: list[str] = []
    skip_next = False

    for part in parts:
        if skip_next:
            skip_next = False
            continue

        if part in {"-caption", "--caption", "-icon", "--icon", "-name", "--name"}:
            skip_next = True
            continue

        if part.startswith("%"):
            continue

        cleaned.append(part)

    return cleaned


def shutil_which(binary: str) -> str | None:
    if not binary:
        return None

    if os.path.isabs(binary) and os.access(binary, os.X_OK):
        return binary

    for directory in os.environ.get("PATH", "").split(":"):
        candidate = os.path.join(directory, binary)
        if os.access(candidate, os.X_OK) and os.path.isfile(candidate):
            return candidate

    return None


def parse_desktop_file(path: str):
    parser = configparser.ConfigParser(interpolation=None, strict=False)
    parser.optionxform = str

    try:
        with open(path, "r", encoding="utf-8", errors="ignore") as handle:
            parser.read_file(handle)
    except Exception:
        return None

    if not parser.has_section("Desktop Entry"):
        return None

    entry = parser["Desktop Entry"]

    if entry.get("Type", "Application") != "Application":
        return None
    if entry.get("NoDisplay", "false").lower() == "true":
        return None
    if entry.get("Hidden", "false").lower() == "true":
        return None

    try_exec = entry.get("TryExec", "").strip()
    if try_exec and not shutil_which(try_exec):
        return None

    name = entry.get("Name", "").strip()
    exec_line = entry.get("Exec", "").strip()
    icon = entry.get("Icon", "").strip() or None

    if not name or not exec_line:
        return None

    cmd_list = clean_exec(exec_line)
    if not cmd_list:
        return None

    return {
        "name": name,
        "cmd_list": cmd_list,
        "icon": icon,
        "cmd_id": os.path.basename(cmd_list[0]),
    }


def get_apps() -> list[dict]:
    apps: list[dict] = []
    seen: set[tuple[str, tuple[str, ...]]] = set()

    for app_dir in APP_DIRS:
        if not os.path.isdir(app_dir):
            continue

        for filename in sorted(os.listdir(app_dir)):
            if not filename.endswith(".desktop"):
                continue

            app = parse_desktop_file(os.path.join(app_dir, filename))
            if not app:
                continue

            key = (app["name"].lower(), tuple(app["cmd_list"]))
            if key in seen:
                continue

            seen.add(key)
            apps.append(app)

    apps.sort(key=lambda item: item["name"].lower())
    return apps


def load_icon(name: str | None):
    if not name:
        return None

    theme = Gtk.IconTheme.get_default()
    if theme is None:
        return None

    try:
        if os.path.isabs(name) and os.path.exists(name):
            return GdkPixbuf.Pixbuf.new_from_file_at_size(name, ICON_SIZE, ICON_SIZE)
        return theme.load_icon(name, ICON_SIZE, Gtk.IconLookupFlags.FORCE_SIZE)
    except Exception:
        return None


class AppButton(Gtk.Button):
    def __init__(self, app: dict) -> None:
        super().__init__()
        self.app = app

        self.set_relief(Gtk.ReliefStyle.NONE)
        self.set_can_focus(False)
        self.set_size_request(BUTTON_WIDTH, BUTTON_HEIGHT)
        self.get_style_context().add_class("app-button")

        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        box.set_halign(Gtk.Align.CENTER)
        box.set_valign(Gtk.Align.CENTER)

        icon_pixbuf = load_icon(app["icon"])
        if icon_pixbuf is not None:
            image = Gtk.Image.new_from_pixbuf(icon_pixbuf)
        else:
            image = Gtk.Image.new_from_icon_name("application-x-executable", Gtk.IconSize.DIALOG)

        label = Gtk.Label(label=app["name"])
        label.set_line_wrap(True)
        label.set_max_width_chars(16)
        label.set_justify(Gtk.Justification.CENTER)
        label.set_halign(Gtk.Align.CENTER)
        label.get_style_context().add_class("app-label")

        box.pack_start(image, False, False, 0)
        box.pack_start(label, False, False, 0)
        self.add(box)

        self.connect("clicked", self.on_clicked)

    def on_clicked(self, _widget) -> None:
        env = os.environ.copy()
        override = ENV_OVERRIDES.get(self.app["cmd_id"])

        if isinstance(override, dict):
            env.update({str(k): str(v) for k, v in override.items()})

        try:
            subprocess.Popen(self.app["cmd_list"], env=env, start_new_session=True)
            send_controller({"cmd": "app_launched", "app": self.app["cmd_id"]})
            Gtk.main_quit()
        except Exception as exc:
            print(f"Failed to launch {self.app['name']}: {exc}")


class Launcher(Gtk.Window):
    def __init__(self) -> None:
        super().__init__(type=Gtk.WindowType.TOPLEVEL)

        self.set_title("lumo-launcher")
        self.set_decorated(False)
        self.set_accept_focus(True)
        self.set_can_focus(True)
        self.set_focus_on_map(True)
        self.set_app_paintable(True)
        self.set_skip_taskbar_hint(True)
        self.set_skip_pager_hint(True)

        screen = self.get_screen()
        if screen is not None:
            visual = screen.get_rgba_visual()
            if visual is not None:
                self.set_visual(visual)

        if GtkLayerShell.is_supported():
            GtkLayerShell.init_for_window(self)
            GtkLayerShell.set_namespace(self, "lumo-launcher")
            GtkLayerShell.set_layer(self, GtkLayerShell.Layer.OVERLAY)
            GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.TOP, True)
            GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.BOTTOM, True)
            GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.LEFT, True)
            GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.RIGHT, True)
            GtkLayerShell.set_margin(self, GtkLayerShell.Edge.TOP, TOP_MARGIN)
            GtkLayerShell.set_exclusive_zone(self, 0)
            layer_set_keyboard(self, True)
        else:
            self.fullscreen()

        self._last_cols = 0
        self.apps = get_apps()
        self.load_css()

        overlay = Gtk.Overlay()
        self.add(overlay)

        background = Gtk.EventBox()
        background.set_visible_window(False)
        background.connect("button-press-event", self.on_background_click)
        overlay.add(background)

        outer = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        outer.set_border_width(WINDOW_PADDING)
        overlay.add_overlay(outer)

        header = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
        header.get_style_context().add_class("header")

        title = Gtk.Label(label="Applications")
        title.get_style_context().add_class("title")
        title.set_halign(Gtk.Align.START)

        header.pack_start(title, False, False, 0)
        outer.pack_start(header, False, False, 0)

        scroll = Gtk.ScrolledWindow()
        scroll.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        scroll.set_overlay_scrolling(True)
        scroll.set_hexpand(True)
        scroll.set_vexpand(True)
        outer.pack_start(scroll, True, True, 0)

        self.grid = Gtk.Grid()
        self.grid.set_row_spacing(GRID_SPACING)
        self.grid.set_column_spacing(GRID_SPACING)
        self.grid.set_halign(Gtk.Align.CENTER)
        self.grid.set_valign(Gtk.Align.START)
        self.grid.set_margin_top(12)
        self.grid.set_margin_bottom(12)
        scroll.add(self.grid)

        swipe = Gtk.GestureSwipe.new(self)
        swipe.connect("swipe", self.on_swipe)

        self.connect("key-press-event", self.on_key)
        self.connect("size-allocate", self.on_size_allocate)
        self.connect("destroy", self.on_destroy)

    def load_css(self) -> None:
        css = b"""
        window {
            background: rgba(12, 12, 12, 0.88);
        }
        .header {
            padding: 8px 8px 18px 8px;
        }
        .title {
            color: #ffffff;
            font-size: 22px;
            font-weight: 700;
        }
        .app-button {
            background: rgba(255, 255, 255, 0.05);
            border-radius: 18px;
            padding: 10px;
        }
        .app-button:hover {
            background: rgba(255, 255, 255, 0.11);
        }
        .app-label {
            color: #ffffff;
            font-size: 12px;
        }
        """

        provider = Gtk.CssProvider()
        provider.load_from_data(css)

        screen = Gdk.Screen.get_default()
        if screen is not None:
            Gtk.StyleContext.add_provider_for_screen(
                screen,
                provider,
                Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION,
            )

    def calc_columns(self, width: int) -> int:
        usable = max(1, width - (WINDOW_PADDING * 2))
        slot = BUTTON_WIDTH + GRID_SPACING
        cols = max(MIN_COLS, usable // max(1, slot))
        return max(1, cols)

    def populate_grid(self, cols: int) -> None:
        for child in self.grid.get_children():
            self.grid.remove(child)

        for index, app in enumerate(self.apps):
            self.grid.attach(AppButton(app), index % cols, index // cols, 1, 1)

        self.grid.show_all()

    def on_size_allocate(self, _widget, allocation) -> None:
        cols = self.calc_columns(allocation.width)
        if cols != self._last_cols:
            self._last_cols = cols
            self.populate_grid(cols)

    def on_swipe(self, _gesture, velocity_x: float, velocity_y: float) -> None:
        if velocity_y > 0 and abs(velocity_y) > abs(velocity_x):
            send_controller({"cmd": "close_launcher"})
            Gtk.main_quit()

    def on_background_click(self, *_args):
        send_controller({"cmd": "close_launcher"})
        Gtk.main_quit()
        return True

    def on_key(self, _widget, event) -> bool:
        if event.keyval == Gdk.KEY_Escape:
            send_controller({"cmd": "close_launcher"})
            Gtk.main_quit()
            return True
        return False

    def on_destroy(self, *_args) -> None:
        send_controller({"cmd": "close_launcher"})


if __name__ == "__main__":
    win = Launcher()
    win.show_all()
    win.present()
    Gtk.main()

