# Hybrid Runtime Provenance and Distribution Scope

Unified fetch manifest: `configs/hybrid-dependencies.json`; Windows preparation entrypoint and usage are in [DEPENDENCIES](DEPENDENCIES.md).

| Category | Pinned Source | Role |
|---|---|---|
| Android / AOSP | mica 16471258, Cuttlefish 16373615, AOSP HIDL android-16.0.0_r1, frameworks/av e2f098935447ca4945946de5cb69db843fe3f003 | System partitions, software security HALs, source-built HIDL and Stagefright/P010 |
| Quest 2 v77 donor | build 51584880050400150; third-party OTA URL and hash in quest2-full-donor-candidate.json | Proprietary core including gralloc/metadata, libgsl, native QTI Vulkan/QGL; Hybrid does not use its EGL/GLES |
| QTI CAF + AOSP source | SM8350 display e9bbe9a980d408af36b52f2abaae7c35647b7425; SM8250 media 05f3cd8dbfa66f2e79ffda3cb294697374510160; mapper5 AOSP interfaces | Builds HWC/SDM, mapper/allocator, and OMX against current platform ABI |
| Mesa / libdrm | 26.2.4 / 2.4.133, NDK r28c; graphics-sources.lock.json | EGL/GLES, Turnip, and DRM |
| Repository source | probe/ + scripts/ | Vulkan routing, gralloc/layout/backlight, and dma_heap/ION memory adaptation |

OEM firmware such as Surface audio and SLPI are shared hardware inputs separate from the Android runtime sources above. See [IMAGE](IMAGE.md) for image artifact details.

Public kernel rebuild inputs are listed in `configs/hybrid-kernel-source.json`. The external archive contains source code and three DT overlays corresponding to adjacent DTS files, without OEM firmware, ELFs, driver binaries, or credentials; see [Archive Notes](../source-dependencies/kernel-6.18/README.md) for scope. Original copyrights and licenses remain intact, and the KernelSU LICENSE is included in the kernel patches.

---

## Earlier Provenance Records (Historical Baseline)

Pinned sources are listed in `configs/sources.lock.json`, `configs/graphics-sources.lock.json`, and `configs/probe-inputs.sha256`.

| Component | Source / Version |
| --- | --- |
| linux-surface kernel | spx/v6.3, b86f786594dc9e53c57fcfa2aa58ada9541506bb, plus local patches |
| qzed ARM64 config | 7281316889197641763c29242c0120de70c3db11 |
| gbos-vm reference | 3b08ed29a73858644c65f24e06ee2a7cfb7e3774 |
| Googlebook recovery image | mica 16471258 |
| Cuttlefish donor | aosp_cf_arm64_only_phone-userdebug 16373615 |
| Mesa / NDK | 26.2.4 / r28c |
| minigbm | 455b7456397dff01443584b09e2fd594079e905c |
| IPTSd | 3.1.1, with contact-state patch |

## Distribution Scope and Licensing

Recovery images, proprietary firmware, driver binaries, full disk images, third-party source archives (except the audited 6.18 kernel/DT archive above), SSH identities/public keys, Wi-Fi credentials, and raw logs are not published; builders obtain inputs independently and comply with their terms.

Upstream components and derived code retain their respective copyrights and licenses; fulfill license obligations per component before redistributing binaries.
