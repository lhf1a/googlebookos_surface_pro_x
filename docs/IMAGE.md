# Image Policy, Layout, and Touch Calibration

The default build is Hybrid, with SQ1/SQ2 sharing Linux `6.18.60-sq2-android1`. See [BUILD](BUILD.md) for build commands.

## First-Boot Policy

After Android finishes booting and the current user is user 0, `surface-first-boot`:

- Sets `device_provisioned=1` / `user_setup_complete=1`;
- Disables installed setup wizards and launches HOME;
- Writes `/data/system/surface-first-boot-v1` on success so subsequent boots skip it, retrying on next boot if a command fails.

The existing HOME fix is preserved without overriding the user's chosen launcher. `PRIVACY.json` records `first_setup_preserved=false` and `first_setup_skipped=true`.

The image strips inherited accounts, Wi-Fi, and SSH credentials, locks root, disables SSH by default, and uses fresh `userdata`/`metadata` without disabling captive portal probing or pre-provisioning networks. Skipping Android first-time setup does not skip the standalone touch calibration wizard.

## Touch Calibration Wizard

Built from `probe/touch-calibration/` source with a bilingual Chinese/English UI, preserving swipe, rest, single-finger tap, coordinate, and thumb detection flows along with confirm/undo behavior. Automatic iterative optimization is disabled by default: zero false touches and residual signals are reported separately, no re-test is forced without parameter changes, and coordinate/thumb tests are on-demand; learned profiles are stored under a key derived from device + screen + descriptor.

When only wizard Java/Manifest files change, `full_image_reuse.prepared_runtime` invalidates the old APK for rebuild while reusing graphics glue libraries and the `userdata` resize tool.

## Partition Layout and Verification

Default slim 5-partition layout with 8 GiB raw disk capacity:

- `super`: 3.40 GiB, 6 `_a` EROFS slices only
- `metadata`: 64 MiB
- `misc`: 4 MiB
- `SQ2PROBE`: 1 GiB
- `userdata`: 3.53 GiB, auto-expands on first boot

Verify compressed image integrity:

```sh
gzip -t surface-pro-x-6.18-hybrid-generic.img.gz
```
