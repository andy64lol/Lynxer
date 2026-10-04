# Bob — TODO

Planned work for the Lynxer package manager. Bob can scaffold modules and
publish immutable module archives to GitHub Releases.

- [x] **GitHub Releases integration.** Bob now uses GitHub Releases to host `.zip` packages.
  - Fetch the latest release from the GitHub API.
  - Select the correct `.zip` asset from the release.
  - Verify metadata/checksum before installation.

- [x] **CLI configuration.** `bob config set rest-api|github-token` stores the
  REST registry link and the GitHub token in `~/.bob/config.json` (env
  overrides: `BOB_REST_API`, `GITHUB_TOKEN`). `bob install` resolves a package
  through the REST registry (`POST /api/resolve`) when one is configured.

- [ ] **Registry hosting.** `lynxer-registry/` is now a Netlify Function
  (`POST /api/resolve`) and runs locally under `netlify dev`; it still needs to
  be deployed to a Netlify site (see the Server section of the root
  [todo.md](../todo.md)). Publish the production link and consider a built-in
  default for `rest-api`.

- [ ] **Package download and local layout.** Define installation under
  `bob/packages/`; verify the downloaded archive digest against GitHub
  metadata and the lock file before extracting any files.
- [ ] **`bob add` / `bob update`.** `bob install <name> <version>` already
  downloads a release asset into `bob/packages/`; still to do: resolve
  `[dependencies]` in `bob/bob.toml`, write `bob/bob-lock.toml`, and implement
  `add` and `update`.
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
