```bash
#!/bin/bash

set -e

echo "Stopping PiVerse..."

# Stop PiVerse if it is running from this terminal/process tree.
sudo pkill -TERM -f "python main.py" 2>/dev/null || true

sleep 2

# If anything is still using the DRM device, show it.
if sudo fuser /dev/dri/card0 >/dev/null 2>&1; then
    echo "Waiting for PiVerse/DRM users to exit..."

    sleep 2
fi

echo "Starting graphical desktop..."

sudo systemctl start display-manager

echo "Desktop start requested."
echo "Give it a few seconds for the desktop to appear."
```
