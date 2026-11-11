# Paused KGSL/SDE work-in-progress — 2026-11-11

These are source-only snapshots, **not accepted/integrated patches**. They are not
referenced by the normal KGSL/SDE configs. Preserve them for resumption/review.

- Integrated candidate remains KGSL58 / SDE17; SDE whole build fails (exit2).
- IRQ/provider and prepared-vblank implementation is incomplete.
- Mode kernel/catalog/bridge/consumer migration remains incomplete; userspace
  library build success is not a paired runtime acceptance.
- MST fixed-primary must preserve persistent connector identity. Core and driver
  deltas require a newly built matched kernel and full lifecycle verification.
- GEM/private-owner candidate is unfinished and must not be promoted as safe unbind.
- Groups have different parents and overlapping files. **Do not blindly stack**
  them; use MANIFEST.json before/after hashes and merge semantically.
- Only text source deltas are committed. Local archives/evidence, private binaries,
  firmware, credentials and raw logs stay ignored outside this directory.

Each saved patch was reapplied with `patch --batch --fuzz=0 -p1` to a separate
copy of its changed parent files and reconstructed the exact candidate hashes.
This is save integrity verification, not testing the unfinished implementation.
