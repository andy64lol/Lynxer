# Bob — TODO

Planned work for the Lynxer package manager. There is no package registry yet,
so the only commands are `bob --ver` and `bob --init`.

- [ ] **Package registry and download.** Define the registry protocol, the
  on-disk package layout under `bob/packages/`, and checksum/verification before
  adding any download command.
- [ ] **`bob add` / `bob install` / `bob update`.** Resolve `[dependencies]` in
  `bob/bob.toml`, write `bob/bob-lock.toml`, and populate `bob/packages/`.
- [ ] **Project source scaffold.** `bob --init` writes `bob/bob.toml` and
  `bob/bob-lock.toml`; add the source layout (a `main.lynx` entry point) once
  the project model is settled.
- [ ] **Version pinning.** `bob --ver` reports a compile-time constant for the
  supported Lynxer version; derive it from the Lynxer release so the two cannot
  drift.
- [ ] **Release and CI.** Package Bob as its own artifact and add CI; decide how
  a released Bob discovers which Lynxer it targets.
