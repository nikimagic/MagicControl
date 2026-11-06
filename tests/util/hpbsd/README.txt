Exact HPBSD hardware diagnostic. Default: getters only.
--fan-cycle is an explicit physical write experiment; owner authorisation is
required. It checks Max10s, Default5s, Manual65/65 8s, Min8s, Default8s through
the production HpbsdFanController and requires canonical Default at admission.
Every normal exit/exception attempts its ownership-aware Default cleanup.
Do not run while another HPBSD fan-control process is active.
Build with CMake here after a completed common Windows product build.
This probe is internal; do not include it in release/update packages.

--curve-cycle applies explicit native8/3point curves with ordinary boundaries
32/34C (within the shared20..100C input range), samples RPM/PWM/target12seconds and restores/compares the exact policy.
CPU94+/System83+ hot rows and overrides stay immutable. Requires Default and
owner authorisation. Uses the product controller; never writes hardware PWM.

--power-cycle / --power-cycle-low are explicit authorised SMU experiments.
They persist the initial exact STAPM/Slow/Fast milliwatts before any write,
apply20/25/30W or8/10/12W through the shared ApuPowerReader HPBSD path,
sample CPU load/SMU/RAPL/DDR5, restore the baseline and compare all fan-policy
bytes.85C CPU cutoff stops workload and attempts restoration. They do not
write fan/EC/BIOS/voltage/thermal settings. GUI/mctl normal polling never sets
power; HPBSD SETs profile capabilities and startup replay remain disabled.
