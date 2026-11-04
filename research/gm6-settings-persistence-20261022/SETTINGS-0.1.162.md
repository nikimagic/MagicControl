# Final owner storage decision,0.1.162

All known user configuration files use settings/ beside the executable for every
model and both OSes. The0.1.161 LocalAppData preference design is superseded.
This is a path/migration change; no new hardware writer, GPU setting or model
admission was added. GM6 full persistence/Auto/PP0 changes from161 remain.

Windows handoff imports prior settings, then former GM6 shared files when present,
then legacy root files. First-upgrade marker prevents repeating an import into
an already used destination. Initial same-directory startup copies legacy files
without deleting them; settings/ is subsequently authoritative. Read-only helpers
and direct model parsers fall back to root files without migration writes.

Stagers seed clean settings/ defaults or preserve existing legacy settings;
release packager uses the same directory. Updater preserves the entire subtree,
including TXT presets and migration marker. Assets/fonts/logs are independent
of preferences location. A valid machine-bound ROM receipt remains separate,
with compatibility for an old root receipt after the path change.

No disk scan or external last-folder preference pointer is introduced. When the
old program is closed and the new version is extracted elsewhere, copy settings/
with it; active Windows handoff transfers automatically. Unknown historical
unsaved intent cannot be reconstructed. No tests, app, driver or EC execution.
Build/version/hash verification is recorded separately.
