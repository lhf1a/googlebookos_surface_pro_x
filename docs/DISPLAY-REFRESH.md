# SQ2 display refresh investigation — 2026-10-29

**Latest: raw DPCD reads supersede the earlier 2.7-Gbit/s receiver-maximum
inference below. DSC is not advertised, but the eDP supported-rate table
advertises up to 5.4 Gbit/s/lane. Current msm code ignores that table.**

## Raw DPCD result

One-shot read-only module built from matching configured kernel headers;
all ten undefined symbols checked against live __ksymtab exports, vermagic
6.3.8-sq2-android1 SMP mod_unload aarch64. Missing Module.symvers means this
was not a full modpost verification. Module v2 SHA256:
`a0ec57434f246cb44ebb88d572a2c77193f1db11bb609616c40ff49f3434402c`.

Nine 16-byte reads returned rc=16. No DPCD write API is used. The module
locates the existing exact dp-aux child through the platform device and
uses its public aux pointer and drm_dp_dpcd_read(), without rebinding.

- 0x000 = 0x14: DPCD 1.4; 0x001 = 0x0a: legacy 2.7 Gbit/s;
  0x002 = 0xc4: four lanes. Extended receiver block 0x2200 matches.
- 0x060–0x06f are all zero on both reads: DSC decompression support bit
  is clear. 0x090 is also zero (no FEC capability advertised).
- 0x700 = 0x04: eDP 1.4a.
- 0x010 supported link rates are little-endian 16-bit values multiplied
  by 200 kHz: 1.62, 2.16, 2.43, 2.70, 3.24, 4.32, **5.40 Gbit/s/lane**,
  then a zero terminator. This table invalidates treating the legacy
  MAX_LINK_RATE field as the panel's complete advertised maximum.
- 0x100 = 0x0a and 0x101 = 0x84: current legacy 2.7 Gbit/s × four lanes.
  0x115 is zero; current configuration is not using table-based rate selection.

Matching i915 intel_dp.c:2944–2969 demonstrates table parsing and selecting
DP_LINK_RATE_SET when this table exists. Current msm dp_panel.c uses only
legacy MAX_LINK_RATE; table-based selection needs actual driver work, not
merely changing a DT clock. No 5.4-Gbit/s training or overclock was attempted.
At that advertised rate the nominal payload ceiling would be 17.28 Gbit/s,
but it is not a tested working link, and EDID still only advertises 60 Hz.

Both probe loads were followed by successful unload; boot ID unchanged,
screen_only=Y, connector enabled. No boot files/settings/registers written.
Raw captures stay in ignored out/display-refresh/dpcd-read[-v2].txt.
Rebuild with scripts/build-sq2-dpcd-read.sh; while awake and after ABI/export
verification, insmod the temporary module, read its result parameter, then
rmmod sq2_dpcd_read. The sysfs result is a cached snapshot, not repeated I/O.

The next candidate is eDP rate-table negotiation support, not DSC. A successful
higher-rate link would solve transport capacity only; panel scan timing and
actual non-skipping high refresh remain separate, untested requirements.

## Earlier investigation (retained with the correction above)

User authorized progressive refresh testing with rollback. Read-only hardware
inspection found a bandwidth gate before mode switching; no overclock was applied.

## Live observations

- eDP-1 connected, only 2880x1920 advertised.
- EDID manufacturer LGD, product 0x0555; text LGD_MP1.0_ and LP129WT112684.
  These strings are not sufficient to identify a retail replacement-panel SKU.
- EDID range descriptor: 30–60 Hz vertical, 360 MHz maximum pixel clock.
- Active timing: 355500 kHz, horizontal 2880/2928/2960/3000,
  vertical 1920/1941/1946/1975; exactly 60 Hz; 24 bits per pixel.
- msm_dp debug reports link_info 270000 / four lanes, and actual link
  bw_code 10 / four lanes / lclk 270000000.
- No drm_dp_aux userspace device is exposed, so raw DPCD receiver capability
  bytes were not independently read. Do not equate current link rate with an
  independently proven physical receiver maximum.
- screen_only=Y. No display override, Android setting, boot image or disk was
  changed. Temporary debugfs mount removed after inspection.

## Bandwidth gate

At four lanes of 2.7 Gbit/s with 8b/10b coding, payload ceiling is 8.64 Gbit/s.
Current timing consumes 355.5 MHz × 24 = 8.532 Gbit/s.
Even ignoring all blanking, native 2880×1920 at 65 Hz and 24 bpp requires
8.626176 Gbit/s, leaving effectively no practical timing overhead. 70 Hz
already requires 9.289728 Gbit/s without blanking.
Keeping the existing timing totals requires 9.243/9.954/10.665/12.798/17.064
Gbit/s at 65/70/75/90/120 Hz respectively. All exceed the observed link.

Therefore no progressive live overclock was attempted: existing link/timing
cannot carry even the first 65 Hz step. This is a capability/bandwidth
assessment, not an observed high-refresh failure or a successful overclock.

Next prerequisite is reading receiver DPCD and matching running driver/DT
link limits, to establish whether higher link training is possible. Only then
build an independently timed restore mechanism and temporary mode test.
Lowering color depth/resolution is a separate quality tradeoff; it does not
prove this panel will scan above its advertised 60 Hz. Actual frame skipping
requires external visual measurement, not just a reported refresh counter.

## Receiver versus driver follow-up

Live kernel is 6.3.8-sq2-android1. Inspected the documented matching
linux-surface b86f786 source in the sq2-thermal build cache, plus the live DT:

- Enabled controller is ae9a000, compatible qcom,sc8180x-edp. The other
  two DP controllers are disabled. Legacy data-lanes contains 0,1,2,3.
- Its output endpoint has no link-frequencies property. Live OPP table
  includes 270/540/810 MHz link-clock entries; this is not a receiver guarantee.
- dp_parser.c:97–139 reads the endpoint maximum, otherwise defaults to
  DP_LINK_RATE_HBR2=540000 (5.4 Gbit/s per lane).
- dp_panel.c:75–84 derives link_info.rate from receiver DPCD MAX_LINK_RATE,
  then clamps to the parser maximum. Extended receiver capability selection
  occurs before this. No other assignment to the panel link_info.rate was
  found in this source directory.
- dp_debug.c prints panel->link_info.rate separately from the active
  link->link_params.rate. Both were 270000 on the device. Training fallback
  works on link_params; the capability field is not merely the selected
  clock for the current 60 Hz mode.
- PHY source includes 5.4 and 8.1 Gbit/s handling; simply adding a faster
  OPP or changing the DT default does not override a lower receiver claim.

Inference from this source and live DT/debug data: receiver-advertised
maximum is 2.7 Gbit/s per lane, not a DT cap at 2.7. Raw DPCD bytes remain
unread: CONFIG_DRM_DP_AUX_CHARDEV is disabled. No binary disassembly or
independent read of cached receiver bytes was performed, so this is a
source-based inference, not a raw register dump or physical overclock limit.

Native resolution at 90 Hz needs 11.943936 Gbit/s even with zero blanking
and 24 bpp; 120 Hz needs 15.925248 Gbit/s. Even 18 bpp at 90 Hz needs
8.957952 Gbit/s before blanking, exceeding 8.64. Thus reducing color depth
alone cannot deliver native 90/120 Hz on this advertised link. No live
overclock or forced unsupported link training was performed. Boot ID and
screen-only guard remained unchanged.

Driver testing caveat: dp_bridge_mode_valid() returns MODE_OK early for
eDP, relying on the panel mode list. Therefore a synthetic mode passing
that callback would not validate link bandwidth or panel acceptance.
