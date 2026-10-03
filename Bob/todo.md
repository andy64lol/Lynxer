# Bob — TODO

Planned work for the Lynxer package manager. There is no package registry yet,
so the only commands are `bob --ver` and `bob --init`.

- [ ] **Package registry and download.** Define the registry protocol, the
  on-disk package layout under `bob/packages/`, and checksum/verification before
  adding any download command.
- [ ] **`bob add` / `bob install` / `bob update`.** Resolve `[dependencies]` in
  `bob/bob.toml`, write `bob/bob-lock.toml`, and populate `bob/packages/`.
- [ ] **Application source scaffold.** `bob --init --module` now writes
  `module.toml` and `src/main.lynx` for a reusable module; `bob --init` still
  writes only `bob/bob.toml` and `bob/bob-lock.toml`. Add the application
  entry-point layout (a `main.lynx`) once the project model is settled.
- [ ] **Version pinning.** `bob --ver` reports a compile-time constant for the
  supported Lynxer version; derive it from the Lynxer release so the two cannot
  drift.
- [ ] **Release and CI.** Bob has its own workflows
  (`build-bob-{amd,arm,windows-amd,windows-arm}.yml`) that build it and run the
  self-check, scoped to `Bob/**` and separate from the Lynxer jobs. Still to do:
  publish a release artifact, add a macOS job, and decide how a released Bob
  discovers which Lynxer it targets.
