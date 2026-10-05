# The Bob registry

The Bob registry tells `bob install <name> <version>` where a package lives. It
is **not** a package host — the archives live on GitHub Releases. It is a small
database plus a public resolve API and a browsable index.

```text
bob install ──POST /api/resolve──▶ Netlify Function ──▶ Supabase (public.modules)
                                      (the API)              ▲
                                                             │
                          browser ──▶ Bob Index (Render) ──────┘
```

- **Supabase** (`andy64lol's Project`) holds the `public.modules` table — one row
  per module. RLS allows public `SELECT`.
- **Netlify** stays the API: `POST /api/resolve` (+ `/health`), reading Supabase
  with the public anon key.
- **Render** hosts the **Bob Index**, a PyPI-like read-only page — see
  [bob-index.md](bob-index.md).

## Data

`public.modules`:

| Column | Meaning |
| --- | --- |
| `name` | Primary key; the module name. |
| `repository` | GitHub repository (`owner/repo`). |
| `version` | Latest published version. |
| `description` | Short summary for the index. |
| `created_at`, `updated_at` | Timestamps. |

Schema and seed: [`../supabase/migrations/0001_modules.sql`](../supabase/migrations/0001_modules.sql).

## API

| Method | Path | Body | Response |
| --- | --- | --- | --- |
| `POST` | `/api/resolve` | `{"name":"foo"}` | `200 {"owner", "repository", "version"}` |
| `GET` | `/health` | — | `200 {"status":"ok"}` |

Errors: `400` (missing name / bad JSON), `404` (unknown package), `405` (wrong
method), `502` (database unreachable). The function is
`lynxer-registry/netlify/functions/resolve.js`.

```console
$ curl -sX POST https://lynxer.netlify.app/api/resolve \
    -H 'Content-Type: application/json' -d '{"name":"foo"}'
{"repository":"foo","owner":"andy64lol","version":"0.1.0"}
```

## How Bob uses it

`bob install` resolves a name in this order: the **REST registry** (when
`rest-api` is configured), then the local `~/.bob/registry.json` mappings, then a
small set of built-in defaults. Set the registry with
`bob config set rest-api https://lynxer.netlify.app` (see [bob.md](bob.md)).

## Deployment

- **Supabase**: `supabase link --project-ref <ref>` then `supabase db push`
  applies everything under `supabase/migrations/`. Reads use the public
  anon/publishable key; the secret/service key is never sent to a client.
- **Netlify**: the `lynxer` site. Set the environment and deploy:

  ```console
  $ netlify env:set SUPABASE_URL https://<ref>.supabase.co
  $ netlify env:set SUPABASE_ANON_KEY <anon-key>
  $ netlify deploy --prod
  ```

- **Render**: the root [`../render.yaml`](../render.yaml) Blueprint defines the
  `BobI` service (see [bob-index.md](bob-index.md)).

## Adding a package

There is no upload flow yet. Add a row to `public.modules` (SQL editor, or a
migration):

```sql
insert into public.modules (name, repository, version, description)
values ('mymod', 'owner/mymod', '1.0.0', 'What it does')
on conflict (name) do update
  set repository  = excluded.repository,
      version     = excluded.version,
      description = excluded.description,
      updated_at  = now();
```

It then appears in the Bob Index and resolves for `bob install`.

## Local development

```console
$ cd lynxer-registry && netlify dev      # http://localhost:8888/api/resolve
$ cd BobI && npm install && npm start     # http://localhost:3000
$ bob config set rest-api http://localhost:8888
```

## See also

- [bob.md](bob.md) — the Bob CLI and configuration
- [bob-modules.md](bob-modules.md) — the module format and publish flow
- [bob-index.md](bob-index.md) — the browsable Bob Index
