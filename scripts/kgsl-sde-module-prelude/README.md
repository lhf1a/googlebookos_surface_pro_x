# Host-pinned early module prelude

Generate a direct-insmod dependency plan from a matching ARM64 module tree,
kernel configuration/notes, exact BusyBox and parent init. This avoids the small
BusyBox loader's initial failed insertion before discovering SSAM's CRC dependency.

`generate.py --help` lists required inputs. Always use a fresh output directory.
No device, insertion, kernel build or reboot operation occurs in the generator.
The generated executor checks hashes and loaded notes, serializes callers and
does not retry an uncertain insertion. Original source files are unchanged.

`dummy` remains an explicit optional request classified `CONFIG_DUMMY=n` in this
matched build. Completing available prelude requests does not claim that dummy
or every requested capability exists. It does not prove hardware binding.

This is an independent startup foundation, not a production approval generator
or a complete Android initramfs. Review evidence is in ignored
`out/kgsl-resume-2/round6/module-prelude`; see
`docs/KGSL-SDE-BOOT-FLOW-20261111.md` for remaining hardware/input gates.
