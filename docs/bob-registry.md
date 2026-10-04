# The Bob registry

The Bob registry maps a package **name** to the GitHub repository that hosts it,
so `bob install <name> <version>` knows where to fetch the release from. It is a
small REST service, not a package host — the archives themselves live on GitHub
Releases.

## Entries

The registry is a single JSON document: a `packages` map keyed by name, each
entry giving the GitHub repository (`owner/repo`) and the published version.

```json
{
  "packages": {
    "foo": { "repository": "andy64lol/foo", "version": "0.1.0" }
  }
}
```

| Field | Meaning |
| --- | --- |
| `repository` | The GitHub repository (`owner/repo`). A full `github.com` URL also works. |
| `version` | The published version. Optional, echoed back for the caller. |

## API

| Method | Path | Body | Response |
| --- | --- | --- | --- |
| `POST` | `/api/resolve` | `{"name": "foo"}` | `200 {"owner", "repository", "version"}` |
| `GET` | `/health` | — | `200 {"status": "ok"}` |

Errors: `400` when `name` is missing or the body is not JSON, `404` for an
unknown package, `405` for any method other than `POST`.

```console
$ curl -sX POST https://lynxer.netlify.app/api/resolve \
    -H 'Content-Type: application/json' -d '{"name":"foo"}'
{"repository":"foo","owner":"andy64lol","version":"0.1.0"}
```

## How Bob uses it

`bob install` resolves a name in this order: the **REST registry** (when
`rest-api` is configured), then the local `~/.bob/registry.json` mappings, then a
small set of built-in defaults. Set the registry with
`bob config set rest-api <url>` (see [bob.md](bob.md)); `bob registry add` only
manages the local fallback.

## Deployment

`lynxer-registry/` is the service: a Netlify Function
(`netlify/functions/resolve.js`) with the public `POST /api/resolve` path mapped
in `netlify.toml`. It is deployed to **<https://lynxer.netlify.app>**, and the
Netlify site's Git integration rebuilds it on every push to `main`, so adding a
package is just editing `lynxer-registry/registry.json` and committing.

To run it locally:

```console
$ cd lynxer-registry
$ netlify dev            # serves /api/resolve on http://localhost:3000
$ bob config set rest-api http://localhost:3000
```

## Adding a package

1. Publish the module (see [bob-modules.md](bob-modules.md)) to get a GitHub
   Release asset.
2. Add an entry to `lynxer-registry/registry.json`:

   ```json
   "foo": { "repository": "owner/foo", "version": "0.1.0" }
   ```

3. Commit and push; Netlify redeploys automatically.
4. `bob install foo 0.1.0` now resolves through the registry.

## See also

- [bob.md](bob.md) — the Bob CLI and configuration
- [bob-modules.md](bob-modules.md) — the module format and publish flow
