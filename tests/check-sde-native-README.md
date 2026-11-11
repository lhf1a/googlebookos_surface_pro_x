# Native SDE DMA host checks

These checks compile **actual source functions** with explicit host models. They
do not build a kernel/module, exercise hardware DMA/TLB/scanout, prove kernel
concurrency or retirement, or establish secure support. The `.inc.c` files are
intentional harness fragments, not standalone programs. Kernel structures, DMA
operations, attachment final unmap, locks and references are modeled in the test
headers. The provider/VMA joint check uses the real provider and VMA bodies;
the complete GEM finalizer is not included in that check.

Requirements: Python 3, a host GCC-compatible compiler, and its UBSan runtime.
No package installation, device access, private output directory or KOUT is used.
All input sources are read-only; each command requires a fresh output directory.

```sh
python3 scripts/check-sde-native-dma.py --source "$SOURCE" --output "$RESULTS/dma"
python3 scripts/check-sde-native-kms.py --source "$SOURCE" --output "$RESULTS/kms"
python3 scripts/check-sde-native-secure.py --source "$SOURCE" --output "$RESULTS/secure"
python3 scripts/check-sde-native-joint.py --source "$SOURCE" --output "$RESULTS/joint"
```

`SOURCE` can be a prepared source root or its `techpack/display/msm` directory.
For independently developed VMA dependencies, add
`--vma-source "$SUCCESSOR/techpack/display/msm/msm_gem_vma.c"` to the joint command.
The joint check requires the native provider and the successor's checked VMA
publication contract; the legacy provider is covered by the first three checks.
`--cc`, `--sanitize` and `--timeout` are configurable; default sanitization is
UBSan. `--sanitize address,undefined` also enables ASan. An optional
`--reference-result "$PREVIOUS/RESULT.json"` compares the literal final test
stdout, stderr and exit status with either the historical or current result
schema; a mismatch makes the checker fail.

## Source identities and flags

The checker reads `MSM_BO_STOLEN`, `MSM_BO_KEEPATTRS` and `MSM_BO_EXTBUF` from the
target's actual `msm_gem.h`, accepts complete integer literals only, validates
single-bit/non-aliasing masks, and injects those exact tokens into the generated
C. The fixtures do not supply numerical BO masks. An unsupported expression is
an explicit preflight failure, not a guessed flag value. The exact domain enum
is read from `msm_mmu.h`; a changed order/initializer requires fixture review.
Public DMA direction constants and kernel layout/API substitutes remain models.
The baseline-only legacy LLC attribute token is a compile model, not a claim
that that private kernel attribute exists on modern Linux.

Each output contains generated `harness.c`, the executable, separate compile/run
stdout/stderr files, and `RESULT.json` with literal commands/statuses, source and
fixture hashes, actual constants, counts, and comparison results. Exit 0 means
all assertions/comparison passed, 1 means compile/run/assertion/comparison
failure, and 2 means unsupported/missing input or output collision. Compiler
exit statuses, including unavailable compiler (127) and timeout (124), remain
literal in the result. Existing output directories are never overwritten.

## Matrix retained from the original transaction

| Check | Legacy baseline / actual rollback | Native candidate |
|---|---:|---:|
| Provider common cases | 4 green / 5 red, exit 1 | 9 green, exit 0 |
| Additional native provider cases | not applicable | 21 green, exit 0 |
| Actual KMS functions | 4 green / 11 red, exit 1 | 15 green, exit 0 |
| Actual secure admission function | 2 green / 3 red, exit 1 | 5 green, exit 0 |
| Native provider + checked VMA | not applicable | 14 green, exit 0 |

The KMS fixture supplies provider/allocation/snapshot errors to exercise the
actual publication/unwind functions; it does not substitute for the joint check.
The secure fixture checks translated/direct/global secure requests are rejected,
not that secure memory or transitions work. Unsupported detach/splash/NRT/private
cache attributes must not be confused with successfully implemented capabilities.
