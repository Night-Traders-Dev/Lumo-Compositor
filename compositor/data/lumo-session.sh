#!/bin/sh
exec /usr/local/bin/lumo-compositor --backend drm --debug --shell /usr/local/bin/lumo-shell 2>/tmp/lumo-session.log
