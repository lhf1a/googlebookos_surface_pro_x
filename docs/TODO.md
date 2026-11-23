# TODO

Lists only unfinished and unverified items. See the corresponding feature docs for completed items and `git log` for history.

## P0: Generic SQ1/SQ2 Delivery Remediation

Rules: [DEVELOPMENT](DEVELOPMENT.md), and `SPX-PORTABLE-SQ1-SQ2` in root `AGENTS.md`.

- Audit standard builds, active initramfs, EFI/DT, and runtime helpers to remove single-device admission/resource assumptions on serial numbers, fixed source disk UUID/ESP markers, addresses, user IDs, and one-time enumerated values.
- Separate lab SSH private keys/host identities, passwords/shadow, saved Wi-Fi state, and target-unique data from runtime/donor so generic images do not inherit them.
- Switch storage/ESP/user/audio/touch resources to dynamic discovery while keeping live target verification and write-safety guards for disk writes.
- Complete representative cross-device regression on a single artifact across SQ1/SQ2 × default/fallback entries, covering different identities, capacities, valid resource numbers, and upgrades over existing data.
- Spot-check remaining hardcoded device paths, phandles, event/card numbers, and single-device assumptions.

## Delivery Acceptance

- [ ] Fresh boot the current Hybrid full image on SQ1 and SQ2 (each with fresh userdata), recording boot ID, first-boot policy, and launcher result.
- [ ] Upgrade path over existing data: preserve user credentials, Wi-Fi, and launcher selection without re-overwriting settings.
- [ ] SQ1 long-duration run and suspend/resume; SQ2 long-term power consumption and multi-cycle resume.
- [ ] Anonymity review of public images (no SSH/Wi-Fi/account/FSG/NV residuals).

## Camera

- [ ] IR capture and Hello authentication.
- Known limitations: occasional CCI open/switch timeout; low HD software ISP framerate; image quality/exposure and rear AF need improvement.

## Sensors

- [ ] SQ1 auto-brightness/auto-rotation on-device acceptance.
- [ ] Sensor recovery after long-term suspend.

## LTE

- [ ] SQ2 new collection entrypoint and full new image fresh-boot acceptance on both devices.
- [ ] Preserve local FSG/UUID cache reuse path without embedding single-device calibration.

## Graphics & Video

- [ ] Wild Life GMEM root cause (currently a sysmem compatibility workaround scoped to A680.1 + 3DMark).
- [ ] Internal panel HDR (currently SDR only, Main10 filtered).
- [ ] Framerate/power under concurrent HD software ISP and video.

## Power

- [ ] Investigate premature `timerfd` wakeups and freezer aborts in `s2idle` loops.
- [ ] Long-term power and battery curve measurements.
