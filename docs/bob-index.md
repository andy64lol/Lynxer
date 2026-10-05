# Bob Index (BobI)

The **Bob Index** is the human-browsable front end for the Bob registry: a small
**PyPI-like** page that lists the Lynxer modules, with a page per module. It is a
Node.js service (Express + `@supabase/supabase-js`) hosted on **Render** and
reading the same Supabase database the resolve API uses. It is **read-only** for
now — there are no uploads.

Source: [`../BobI/`](../BobI/).

## Routes

| Route | Serves |
| --- | --- |
| `GET /` | Index page: a search box (`?q=`) and the module list (name, version, repository, description). |
| `GET /modules/:name` | Module page: metadata, the GitHub link, and the `bob install <name> <version>` command. |
| `GET /api/modules` | JSON list (`?q=` filters by name or description). |
| `GET /api/modules/:name` | JSON for one module (`404` when unknown). |
| `GET /health` | `{"status":"ok"}`. |

## Data

Reads `public.modules` from Supabase (see [bob-registry.md](bob-registry.md) for
the schema). The table allows public `SELECT`, so the service uses only the
public anon/publishable key — never the service-role key.

## Configuration

| Variable | Meaning |
| --- | --- |
| `SUPABASE_URL` | `https://<project-ref>.supabase.co`. |
| `SUPABASE_ANON_KEY` | The public anon (or `sb_publishable_...`) key. |
| `PORT` | Port to listen on (Render sets this; defaults to `3000`). |

Copy `.env.example` to `.env` for local development; `.env` is gitignored.

## Run locally

```console
$ cd BobI
$ npm install
$ cp .env.example .env         # fill in SUPABASE_URL / SUPABASE_ANON_KEY
$ npm start
Bob Index listening on http://localhost:3000
```

## Deploy (Render)

The root [`../render.yaml`](../render.yaml) Blueprint defines the service:

```yaml
services:
  - type: web
    name: bob-index
    runtime: node
    rootDir: BobI
    plan: free
    buildCommand: npm install
    startCommand: npm start
    healthCheckPath: /health
    envVars:
      - key: SUPABASE_URL
        sync: false
      - key: SUPABASE_ANON_KEY
        sync: false
```

Create it once from the Render dashboard (**New → Blueprint → this repository**)
and set `SUPABASE_URL` / `SUPABASE_ANON_KEY` in the service's environment. Render
then builds and deploys on every push to `main`. Validate the Blueprint locally
with `render blueprints validate ./render.yaml`.

## See also

- [bob-registry.md](bob-registry.md) — the registry database and resolve API
- [bob.md](bob.md) — the Bob CLI
- [bob-modules.md](bob-modules.md) — the module format
