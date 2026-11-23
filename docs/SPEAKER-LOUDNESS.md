# SQ2 Built-In Speakers: Loudness and Playback Fixes

## Playback Adaptation

`scripts/prepare-sq2-primary-audio.py` is invoked by `assemble_runtime` on the microphone-fixed donor HAL to adapt built-in playback:
- Changes PRIMARY output nominal latency to 10 ms (480 frames at 48 kHz).
- Sets the built-in playback proxy physical buffer request to at least 5760 frames (4×1440 periods).
- Preserves microphone, USB proxy, MMAP, emulator pacing, and low-level write functions.
- Output patched HAL SHA256: `65ecd30b575ab81517d86456386842faa65496075216c8c6ae8d2dd7ef8af6e1`.

## Stop-Playback Mute Sequence and DSP Boot Check

- **Stop-playback mute sequence**: `patch-sq2-audio-sequencing.py --machine-only` applies only the SDM845 pre-teardown mute patch (leaving WSA881x `PRE_PMD` disabled), writing `0x7c` to mute both sides before stream stop and SoundWire rebuild, then `0xfc` in `prepare` to unmute, while keeping `pmdown_time=0` on the three playback links. See [BUILD](BUILD.md) and [IMAGE](IMAGE.md) for subsequent status.
- **EL2 DSP boot check**: `audio-drivers-start` accepts both `attached` and `running` states for the audio DSP before loading the sound card.

## Speaker Gain Defaults

- In `probe/test2/audio-speaker-route` (`enable` branch), RX7 and RX8 Digital Volume are set to **84** (**0 dB**), a +12 dB increase over the original **72** (−12 dB).
- PA power-up behavior, DAC/COMP/BOOST paired ports, VISENSE, HAL, firmware, microphone, and EQ configs remain unchanged.

## Build Integration

`build-source-boot.py` refreshes `audio-speaker-route`, verifies the packaged script matches source, and records `speaker_rx_raw=[84,84]`, `speaker_rx_db=0`, `speaker_gain_change_db=12`, and `speaker_route_sha256` in the audit. To inspect the final script in a real archive, set `SQ2_AUDIO_BOOT_IMAGE=/absolute/path/to/native-combined-initramfs.cpio.gz`.

## Hardware Note: SQ2 `ldoe14` XOB Supply Rail

- On SQ2, RPMh `cmd-db` configures `ldoe14` (`0x43900`) as an enable-only **XOB** resource (`+0x4` ENABLE only), whereas mainline `qcom-rpmh-regulator.c` maps `ldo14` as `pmic5_pldo_lv` (VRM). Writing to non-existent `VRM_VOLTAGE` (`+0x0`) or `VRM_MODE` (`+0x8`) registers can stall RPMh AMC TCS slots.
- `patches/kernel-6.18-surface-baseline.patch` (`sc8180x-surface-pro-x.dts`) defines `vreg_l14e_1p8: ldo14` (`1800000` uV, `RPMH_REGULATOR_MODE_HPM`, `regulator-boot-on`, `regulator-always-on`), referenced by `left_spkr: speaker@0,3` and `right_spkr: speaker@0,4`; the current Hybrid 6.18.60 EL2 port keeps this DTS unchanged.
