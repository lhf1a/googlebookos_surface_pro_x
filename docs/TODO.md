# TODO

Lists only unfinished and unverified items. See the corresponding feature docs for completed items and `git log` for history.

## P0: Generic SQ1/SQ2 Delivery Remediation

Rules: [DEVELOPMENT](DEVELOPMENT.md), and `SPX-PORTABLE-SQ1-SQ2` in root `AGENTS.md`.

- Complete representative cross-device regression on a single artifact across SQ1/SQ2 × default/fallback entries, covering varying SMBIOS identities, capacities, enumerated resource indices, and upgrades over existing data.
- Spot-check remaining hardcoded device paths, phandles, event/card numbers, and single-device assumptions in boot/runtime scripts.

## Delivery Acceptance

- [ ] Fresh boot the current Hybrid full image on SQ1 and SQ2 (each with fresh userdata), recording boot ID, first-boot policy, and launcher result.
- [ ] Upgrade path over existing data: verify the latest build preserves user credentials, Wi-Fi, and launcher selection without re-overwriting settings across SQ1/SQ2.
- [ ] SQ1 long-duration run and suspend/resume (including the `mmcx` `skip_retention_level` fix); SQ2 overnight standby and long-term power consumption measurements.
- [ ] Pre-release anonymity review of published `.img.gz` artifacts (confirm zero SSH/Wi-Fi/account/FSG/NV residuals).

## Camera

- [ ] SQ1 on-device acceptance for Class 2 IR Face Unlock (`ov7251` multi-orientation matching & liveness).
- Known limitations: occasional CCI open/switch timeout; low HD software ISP framerate; image quality/exposure and rear AF need improvement.

## Sensors

- [ ] SQ1 auto-brightness/auto-rotation on-device acceptance.
- [ ] Sensor recovery after long-term suspend.

## LTE

- [ ] SQ2 new collection entrypoint and full new image fresh-boot acceptance on both SQ1 and SQ2.

## Graphics & Video

- [ ] Internal panel HDR (currently SDR only on panel; Main10/P010 decode is available on standard Surfaces).
- [ ] Framerate/power under concurrent HD software ISP and video.

## Power

- [ ] Multi-hour / overnight `s2idle` wakeup audit and battery discharge curve measurements across SQ1 and SQ2 (post-`timerfd`/`SSAM`/`MMCX` fixes).
