# SQ2 Built-In Microphone

## Routing and Policy

Audio path: WCD9340 DMIC0/DMIC1 -> DEC0/DEC1 -> AIF1 CAP SLIM TX0/TX1 -> MultiMedia2, ALSA card 0 / device 1. `audio-microphone-route` applies control mappings from upstream Surface Pro X UCM v6 with DEC raw gain 96 (+12 dB relative to 84), leaving speaker gain unchanged.

Android `primary_audio_policy_sq2.xml` exposes `Built-In Mic` in native 48 kHz, S16, stereo format; AudioFlinger converts to 16 kHz mono when requested by apps.

## HAL Address Fix

In Cuttlefish donor 16373615's stock HAL, `XsdcConversion.cpp::createAudioDevice` unconditionally overwrites the built-in microphone XML address with `bottom`, causing `StreamPrimary::getCardAndDeviceId` to fall back to `card0/dev0` instead of `card0/dev1`.

`patch-sq2-microphone-hal.py` accepts input SHA256 `fd219b1058eccc701de685cf59d6578e887c11182d877bb4e6c3a78bfd73e279`, verifies ELF sections and instruction context, and patches `cbnz x8, 0xb6260` to `b 0xb6260` at VA/file offset `0xb6120`. This skips the address overwrite for `IN_MICROPHONE` only, preserving `CARD_0_DEV_1` from the XML configuration and producing SHA256 `774cd7e731bd5a5fcda6edbde6e97464bd0e04acc1603c3be7b0dbe7a69561ea`. When rebuilding from source, only microphones lacking a configured address should receive the default address.

Reference sources: [XsdcConversion.cpp](https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/main/audio/aidl/default/XsdcConversion.cpp), [StreamPrimary.cpp](https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/main/audio/aidl/default/primary/StreamPrimary.cpp).

## Build and Packaging

Run `bash scripts/build-sq2-mic-app.sh` in WSL root with Android 35 SDK, build-tools, and JDK to build `out/sensors-mic-camera/app/sq2-mic-test.apk` (signing keys remain in `private/`).

`pack-sq2-microphone.py BASE HAL OUTPUT` packages the audio policy, audio startup helper, microphone route, and patched HAL onto the base image while preserving the local SSH credentials addon as the final member. The startup helper explicitly sets the SELinux context on the HAL binary, restarts the service, and verifies the running HAL process.
