#!/usr/bin/env python3
"""Lumo overlay controller for Sway.

Owns:
- lumo_gesture.py
- lumo_launcher.py

Responsibilities:
- discover Sway IPC socket robustly
- subscribe to Sway events
- start/restart gesture surface
- spawn/kill launcher on demand
- log status for i3blocks debug bar
"""

from __future__ import annotations

import json
import os
import signal
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

import i3ipc


RUNTIME_DIR = os.environ.get("XDG_RUNTIME_DIR", "/tmp")
SOCKET_PATH = os.path.join(RUNTIME_DIR, "lumo-overlay.sock")
LOG_PATH = os.path.join(RUNTIME_DIR, "lumo-overlay.log")

CONFIG_DIR = Path.home() / ".config" / "sway"
GESTURE_SCRIPT = str(CONFIG_DIR / "lumo_gesture.py")
LAUNCHER_SCRIPT = str(CONFIG_DIR / "lumo_launcher.py")

RESTART_BACKOFF_SEC = 0.5


class OverlayController:
    def __init__(self) -> None:
        self.running = True
        self.server_sock: socket.socket | None = None
        self.server_thread: threading.Thread | None = None
        self.event_thread: threading.Thread | None = None

        self.i3: i3ipc.Connection | None = None
        self.i3_socket_path: str | None = None

        self.gesture_proc: subprocess.Popen | None = None
        self.launcher_proc: subprocess.Popen | None = None

        self._launcher_lock = threading.Lock()
        self._gesture_lock = threading.Lock()

    def log(self, message: str) -> None:
        stamp = time.strftime("%Y-%m-%d %H:%M:%S")
        line = f"[{stamp}] {message}"
        print(line, flush=True)
        try:
            with open(LOG_PATH, "a", encoding="utf-8") as handle:
                handle.write(line + "\n")
        except Exception:
            pass

    def discover_sway_socket(self) -> str:
        for var in ("SWAYSOCK", "I3SOCK"):
            value = os.environ.get(var, "").strip()
            if value:
                self.log(f"using {var}={value}")
                return value

        try:
            result = subprocess.run(
                ["sway", "--get-socketpath"],
                capture_output=True,
                text=True,
                timeout=3,
                check=True,
            )
            path = result.stdout.strip()
            if path:
                self.log(f"using sway --get-socketpath => {path}")
                return path
        except Exception as exc:
            self.log(f"sway socket autodetect failed: {exc}")

        raise RuntimeError("SWAYSOCK not set and sway --get-socketpath failed")

    def connect_i3(self) -> None:
        self.i3_socket_path = self.discover_sway_socket()
        self.i3 = i3ipc.Connection(socket_path=self.i3_socket_path, auto_reconnect=True)
        version = self.i3.get_version()
        human = getattr(version, "human_readable", None) or str(version)
        self.log(f"connected to sway ipc: {human}")

    def cleanup_socket(self) -> None:
        try:
            if os.path.exists(SOCKET_PATH):
                os.unlink(SOCKET_PATH)
        except FileNotFoundError:
            pass
        except Exception as exc:
            self.log(f"failed to cleanup ipc socket: {exc}")

    def start_ipc_server(self) -> None:
        self.cleanup_socket()
        self.server_sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.server_sock.bind(SOCKET_PATH)
        self.server_sock.listen(8)
        os.chmod(SOCKET_PATH, 0o600)
        self.log(f"ipc server listening at {SOCKET_PATH}")

        self.server_thread = threading.Thread(target=self.ipc_server_loop, daemon=True)
        self.server_thread.start()

    def ipc_server_loop(self) -> None:
        assert self.server_sock is not None
        while self.running:
            try:
                conn, _addr = self.server_sock.accept()
            except OSError:
                break

            threading.Thread(
                target=self.handle_ipc_client,
                args=(conn,),
                daemon=True,
            ).start()

    def handle_ipc_client(self, conn: socket.socket) -> None:
        with conn:
            try:
                data = b""
                while True:
                    chunk = conn.recv(4096)
                    if not chunk:
                        break
                    data += chunk
                    if b"\n" in chunk:
                        break

                for raw_line in data.splitlines():
                    line = raw_line.decode("utf-8", errors="ignore").strip()
                    if not line:
                        continue
                    try:
                        msg = json.loads(line)
                    except json.JSONDecodeError:
                        self.log(f"bad ipc json: {line!r}")
                        continue
                    self.handle_message(msg)
            except Exception as exc:
                self.log(f"ipc client error: {exc}")

    def handle_message(self, msg: dict) -> None:
        cmd = str(msg.get("cmd", "")).strip()
        self.log(f"ipc message: {msg}")

        if cmd == "open_launcher":
            self.open_launcher()
        elif cmd == "toggle_launcher":
            if self.launcher_running():
                self.close_launcher("toggle")
            else:
                self.open_launcher()
        elif cmd == "close_launcher":
            self.close_launcher("client_request")
        elif cmd == "app_launched":
            app = msg.get("app", "?")
            self.close_launcher(f"app_launched:{app}")
        elif cmd == "ping":
            self.log("ping received")
        else:
            self.log(f"unknown ipc command: {cmd}")

    def gesture_running(self) -> bool:
        return self.gesture_proc is not None and self.gesture_proc.poll() is None

    def launcher_running(self) -> bool:
        return self.launcher_proc is not None and self.launcher_proc.poll() is None

    def spawn_gesture(self) -> None:
        with self._gesture_lock:
            if self.gesture_running():
                return
            self.log("starting gesture surface")
            self.gesture_proc = subprocess.Popen(
                [sys.executable, GESTURE_SCRIPT],
                start_new_session=True,
            )

    def ensure_gesture(self) -> None:
        with self._gesture_lock:
            if self.gesture_running():
                return
            self.log("gesture not running, restarting")
            self.gesture_proc = subprocess.Popen(
                [sys.executable, GESTURE_SCRIPT],
                start_new_session=True,
            )

    def open_launcher(self) -> None:
        with self._launcher_lock:
            if self.launcher_running():
                self.log("launcher already running")
                return
            self.log("starting launcher")
            self.launcher_proc = subprocess.Popen(
                [sys.executable, LAUNCHER_SCRIPT],
                start_new_session=True,
            )

    def close_launcher(self, reason: str = "unknown") -> None:
        with self._launcher_lock:
            if not self.launcher_running():
                return
            assert self.launcher_proc is not None
            self.log(f"closing launcher ({reason})")
            proc = self.launcher_proc
            try:
                proc.terminate()
                proc.wait(timeout=1.5)
            except subprocess.TimeoutExpired:
                self.log("launcher did not exit on terminate, killing")
                proc.kill()
                try:
                    proc.wait(timeout=1.0)
                except subprocess.TimeoutExpired:
                    pass
            except Exception as exc:
                self.log(f"error closing launcher: {exc}")
            finally:
                self.launcher_proc = None

    def close_gesture(self) -> None:
        with self._gesture_lock:
            if not self.gesture_running():
                return
            assert self.gesture_proc is not None
            self.log("stopping gesture")
            proc = self.gesture_proc
            try:
                proc.terminate()
                proc.wait(timeout=1.5)
            except subprocess.TimeoutExpired:
                proc.kill()
            except Exception as exc:
                self.log(f"error stopping gesture: {exc}")
            finally:
                self.gesture_proc = None

    def restart_gesture(self, reason: str) -> None:
        self.log(f"restarting gesture ({reason})")
        self.close_gesture()
        time.sleep(RESTART_BACKOFF_SEC)
        self.spawn_gesture()

    def on_workspace(self, _i3, event) -> None:
        change = getattr(event, "change", "unknown")
        self.log(f"sway workspace event: {change}")
        if change in {"focus", "init", "empty", "move", "rename", "reload"}:
            self.close_launcher(f"workspace:{change}")

    def on_window(self, _i3, event) -> None:
        change = getattr(event, "change", "unknown")
        self.log(f"sway window event: {change}")
        if change in {"focus", "fullscreen_mode", "move", "close", "new"}:
            self.close_launcher(f"window:{change}")

    def on_output(self, _i3, event) -> None:
        change = getattr(event, "change", "unknown")
        self.log(f"sway output event: {change}")
        self.close_launcher(f"output:{change}")
        self.restart_gesture(f"output:{change}")

    def on_input(self, _i3, event) -> None:
        change = getattr(event, "change", "unknown")
        self.log(f"sway input event: {change}")
        self.restart_gesture(f"input:{change}")

    def event_loop(self) -> None:
        assert self.i3 is not None

        self.i3.on("workspace", self.on_workspace)
        self.i3.on("window", self.on_window)
        self.i3.on("output", self.on_output)
        self.i3.on("input", self.on_input)

        self.log("subscribed to sway events: workspace, window, output, input")
        self.i3.main()

    def start_event_loop(self) -> None:
        self.event_thread = threading.Thread(target=self.event_loop, daemon=True)
        self.event_thread.start()

    def monitor_children(self) -> None:
        while self.running:
            try:
                if self.launcher_proc is not None and self.launcher_proc.poll() is not None:
                    code = self.launcher_proc.returncode
                    self.log(f"launcher exited with code {code}")
                    self.launcher_proc = None

                time.sleep(0.4)
            except Exception as exc:
                self.log(f"child monitor error: {exc}")
                time.sleep(1.0)

    def shutdown(self, reason: str) -> None:
        if not self.running:
            return
        self.running = False
        self.log(f"shutdown requested: {reason}")

        try:
            if self.i3 is not None:
                self.i3.main_quit()
        except Exception:
            pass

        self.close_launcher("shutdown")
        self.close_gesture()

        try:
            if self.server_sock is not None:
                self.server_sock.close()
        except Exception:
            pass

        self.cleanup_socket()

    def run(self) -> int:
        self.log("overlay controller starting")
        self.log(f"env SWAYSOCK={os.environ.get('SWAYSOCK', '')}")
        self.log(f"env I3SOCK={os.environ.get('I3SOCK', '')}")
        self.log(f"env WAYLAND_DISPLAY={os.environ.get('WAYLAND_DISPLAY', '')}")
        self.log(f"env XDG_RUNTIME_DIR={RUNTIME_DIR}")

        for path in (GESTURE_SCRIPT, LAUNCHER_SCRIPT):
            if not os.path.exists(path):
                self.log(f"missing required script: {path}")
                return 1

        try:
            self.connect_i3()
        except Exception as exc:
            self.log(str(exc))
            return 1

        self.start_ipc_server()
        self.start_event_loop()

        try:
            self.monitor_children()
        except KeyboardInterrupt:
            self.shutdown("keyboardinterrupt")
        except Exception as exc:
            self.log(f"fatal controller error: {exc}")
            self.shutdown("fatal_error")
            return 1

        self.shutdown("normal_exit")
        return 0


_CONTROLLER: OverlayController | None = None


def _signal_handler(signum, _frame) -> None:
    global _CONTROLLER
    name = signal.Signals(signum).name
    if _CONTROLLER is not None:
        _CONTROLLER.shutdown(name)


def main() -> int:
    global _CONTROLLER
    _CONTROLLER = OverlayController()

    signal.signal(signal.SIGINT, _signal_handler)
    signal.signal(signal.SIGTERM, _signal_handler)

    return _CONTROLLER.run()


if __name__ == "__main__":
    raise SystemExit(main())
