Wujie power readback and Auto regression checks. No hardware/OS adapters.
Configure this directory as a standalone CMake project; build Release, then run
wujie-power-offline and wujie-power-transport.
The first target exercises32000snapshot combinations, unknown/partial reads,
explicit Auto versus native profile, legacy Custom and unchanged model counts.
The second compiles the actual Performance.cpp against a memory EcAccess double:
only783..785 may change, repeated Auto is idempotent, all6injected before/after
write failures restore the original64KiB image, identity and Manual scale guards
remain active. It does not test the real EcAccess transport/driver or physical
watts/fans; no device access is linked into these executables.
