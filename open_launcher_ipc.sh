#!/usr/bin/env sh
python3 - <<'PY'
import json, os, socket
sock_path = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "lumo-overlay.sock")
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(sock_path)
sock.sendall((json.dumps({"cmd": "open_launcher"}) + "\n").encode())
sock.close()
PY
