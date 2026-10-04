# Bob — TODO

Planned work for the Lynxer package manager. Bob can scaffold a module project,
publish immutable module archives to GitHub Releases, and install them by
resolving a package name through the REST registry.

- [x] **GitHub Releases integration.** Bob hosts `.zip` packages on GitHub
  Releases: `bob publish` creates the release asset, `bob install` fetches it by
  tag and reports its SHA-256.

- [x] **CLI configuration.** `bob config set rest-api|github-token` stores the
  REST registry link and the GitHub token in `~/.bob/config.json` (env
  overrides: `BOB_REST_API`, `GITHUB_TOKEN`, and the `BOB_CONFIG_DIR` location).

- [x] **Registry.** `lynxer-registry/` is a Netlify Function (`POST
  /api/resolve`) deployed to <https://lynxer.netlify.app> with Git auto-deploy
  on pushes to `main`. Bob resolves a name through it before falling back to
  `~/.bob/registry.json` and the built-in defaults.

- [ ] **Node-style install layout.** `bob install` currently writes the download
  to `bob/packages/{name}-{version}/archive.zip`. Extract the archive into
  `modules/<name>/` (renaming the archived `manifest.toml` to `module.toml`) so
  the interpreter's `import("<name>")` finds it — rejecting entries that escape
  the module directory and verifying the digest against the registry/lock first.

- [ ] **`bob add` / `bob update`.** Resolve `[dependencies]` in `bob/bob.toml`,
  write `bob/bob-lock.toml`, and implement `add` and `update` (`install` exists).

- [ ] **Application source scaffold.** `bob --init` writes `bob/bob.toml` and
  `bob/bob-lock.toml`; add the application entry-point layout (a `main.lynx`)
  once the project model is settled.

- [ ] **Version pinning.** `bob --ver` reports a compile-time constant for the
  supported Lynxer version; derive it from the Lynxer release so the two cannot
  drift.

- [ ] **Release and CI.** Bob has its own workflows
  (`build-bob-{amd,arm,windows-amd,windows-arm}.yml`) that build it and run the
  self-check, scoped to `Bob/**` and separate from the Lynxer jobs. Still to do:
  publish a release artifact, add a macOS job, and decide how a released Bob
  discovers which Lynxer it targets.
