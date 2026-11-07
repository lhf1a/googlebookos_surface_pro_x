# Private format evidence for a graphics-backend transition

`graphics-backend-cache.sh` accepts optional `--classification-file /absolute/FILE`
for `dry-run` and `apply` (including a pending transaction resume). It does not
load a global allowlist. No application package, Android user, or device identity
is hardcoded. Discovery, CE unlock checks and consumer-stop requirements remain.

The UTF-8/LF TSV begins with this exact line:

```text
graphics-cache-classifications-v1
```

Every subsequent line has five nonempty tab-separated fields:

1. `purpose`: `rebuildable-cache`, `preserve-source-asset`, `preserve-application-data`, or
   `preserve-inactive-backup`.
2. `digest_kind`: `file-sha256`, `tree-fingerprint-v1`, or
   `symlink-target-file-sha256`.
3. `path_hex`: lowercase hexadecimal UTF-8 encoding of the exact absolute path
   in the target Android mount namespace.
4. `sha256`: lowercase 64-character digest of the inspected object.
5. `evidence_id`: a private evidence reference, containing only letters, digits,
   `_ . : / -`. The helper records it but never executes or opens it.

The input is copied into the transaction and hashed before scanning. Paths from
the input are decoded only to compute a private lookup index, never for filesystem
access: only dynamically discovered paths are matched. Ordinary leaves use one
path-byte SHA and shell index lookup instead of reparsing the TSV for each file;
matched rows also compare the complete path bytes. Duplicate, malformed and executable-looking rows are rejected
before any cache move. A mismatched hash/type remains pending, without readiness.
On resume, pass the evidence file explicitly; the old record is not silently
reused. A completed transition remains a no-op, preserving new-backend caches.

## Evidence and scope

- A file SHA is the hash of its exact content. A tree fingerprint uses the helper's
  existing `fingerprint()` format: sorted NUL-delimited relative paths, types,
  mode/UID/GID and content hashes. Obtain it from the actual mounted, unlocked
  tree; a directory name, stat result or partial listing is not a fingerprint.
- Known live cache names retain their existing behavior; a classification cannot
  mark a built-in `GPUCache` as preserved. An unknown rebuildable cache uses the
  existing same-parent move, journal, hash checks and rollback, never deletion.
  Explicit evidence is matched against every dynamically discovered object, not
  only graphics-sounding names: hash-named driver binaries in a mixed resource
  directory can be moved individually while source assets/metadata remain.
- A preserved source directory does **not** suppress descendant inspection.
  Each unknown child still needs its own evidence; known caches remain handled.
  Only an independently proven inactive backup, with its entire tree pinned,
  can exclude descendants. A `before-*` suffix is not proof by itself.
- `preserve-application-data` separately identifies application databases,
  compatibility settings or downloaded media. These are not mislabeled source
  code or disposable compiled shaders. Classification requires positive context
  and format/purpose evidence, not merely an unknown extension; a directory still
  cannot suppress descendant inspection. A graphics-sounding word in a database
  or settings name is not by itself evidence of a compiled graphics cache.
- `symlink-target-file-sha256` is permitted only with `preserve-source-asset`.
  It explicitly authorizes reading the discovered link's resolved **regular
  file** to verify its content hash, such as an inspected system APK. It does not
  follow directory links or permit cache mutation through a link. Unknown
  graphics links remain pending; built-in live cache links cannot use this rule.
- The evidence file and reports contain private application paths/hashes. They
  belong in private transaction storage, not Git or a distributable boot image.
  Hashes bind evidence to actual bytes; they are not device admission rules.

## Format distinctions

The classification input records a prior inspection; the helper does not pretend
to implement every engine's parser. Filenames alone are insufficient evidence.

- [Vulkan's pipeline cache header](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineCacheHeaderVersionOne.html)
  provides little-endian header size/version, implementation IDs and UUID.
  Those fields can identify a real serialized cache, without treating one
  device's IDs as a whitelist.
- [Epic's PSO cache documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/manually-creating-bundled-pso-caches-in-unreal-engine)
  distinguishes user PSO descriptions saved in the game's `Saved` directory
  from lower-level Vulkan/OpenGL compiled caches. Do not equate the formats or
  infer that any arbitrary `pipeline` file is graphics data.
- [RFC 1740](https://www.rfc-editor.org/rfc/rfc1740.html) specifies AppleDouble
  metadata's `00051607` magic and entry layout. Such extracted source metadata
  is not a driver shader cache.
- [Unity's graphics documentation](https://docs.unity3d.com/cn/2022.3/Manual/class-GraphicsSettings.html)
  explains that runtime AssetBundles can supply shader assets. A shader asset
  bundle is not automatically a disposable compiled-driver cache.

Actual shader source, packaged assets, speech-processing pipeline descriptions,
application databases, and user settings must not be removed merely because
their names contain `shader`, `pipeline` or `vulkan`. Insufficient evidence stays
pending and is reported. Passing fixtures does not prove a real all-app cleanup.
