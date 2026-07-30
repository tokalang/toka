# Toka GUI settings demo

Requires macOS, a logged-in desktop session, and a Toka build with the GUI
native-framework support.

```text
cd ../..
cmake -S . -B build
cmake --build build -j4
cd demos/gui-settings
TOKA_LIB=../../lib TOKA_OFFLINE=1 ../../build/bin/toka build
./target/debug/gui_settings_demo
```

It opens a native Metal-backed window. Click the telemetry row to toggle its
state, scroll the list, press Command+R to request a redraw, and close the
window using its system close button.
