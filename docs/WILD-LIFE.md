# SQ2 Wild Life & Turnip GMEM Status

## Current Configuration: Hardware GMEM + Dual Vulkan Routing

- **Kernel GMEM fix**: Linux 6.18 includes `0003 Fix GMEM range on a680` (see `patches/drm-618-a6xx-upstream/`).
- **Turnip 26.2.4**: `scripts/patch-sq2-turnip-gmem.py` and `scripts/build-sq2-turnip.sh` build pure hardware GMEM Mesa Turnip 26.2.4 without the legacy 3DMark sysmem workaround (`--revert` and `--verify-clean` verify clean upstream GMEM source hashes).
- **Dual Vulkan routing (`--graphics hybrid` / `SQ2Hyb`)**: Default builds retain hardware GMEM Turnip 1.3 alongside the Qualcomm Quest 2 v77 native Vulkan routing layer.

## Reference

- Upstream [A680 UBWC/macrotile fix](https://www.mail-archive.com/dri-devel@lists.freedesktop.org/msg504111.html) requires a matching kernel UBWC query interface rather than writing macrotile registers alone.
