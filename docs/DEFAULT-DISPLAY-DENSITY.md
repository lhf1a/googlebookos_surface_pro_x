# Default Display Density

The default build sets `ro.sf.lcd_density=320` via `apply-device-info` before Android `init` reads properties, overriding the legacy default density in the base package. Both `scripts/build.py` and the unified machine backend refresh the helper in the active initramfs. Options such as `--dpi 360` customize the value while keeping resolution and refresh rate unchanged.

Any density saved in existing `userdata` via Display settings or `wm density` takes precedence and is not overwritten on boot; run `wm density reset` to return to the build default.
