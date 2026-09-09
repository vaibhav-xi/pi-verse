```bash
#!/bin/bash

set -e

PROJECT_DIR="$HOME/projects/pi-verse"
VENV="$PROJECT_DIR/venv"

echo "Stopping graphical desktop..."

sudo systemctl stop display-manager 2>/dev/null || true

# Give Xorg/lxsession a moment to release DRM.
sleep 2

# If Xorg is still holding the DRM device, terminate it.
if sudo fuser /dev/dri/card0 >/dev/null 2>&1; then
    echo "Xorg is still using DRM. Stopping remaining graphical processes..."

    sudo pkill -TERM Xorg 2>/dev/null || true
    sudo pkill -TERM lxsession 2>/dev/null || true

    sleep 2
fi

# Verify DRM is available.
if sudo fuser /dev/dri/card0 >/dev/null 2>&1; then
    echo "ERROR: /dev/dri/card0 is still in use:"
    sudo fuser -v /dev/dri/card0
    exit 1
fi

echo "DRM is free."

cd "$PROJECT_DIR"

if [ ! -f "$VENV/bin/activate" ]; then
    echo "ERROR: PiVerse virtual environment not found:"
    echo "$VENV"
    exit 1
fi

source "$VENV/bin/activate"

echo "Starting PiVerse..."
echo

exec python main.py
```
