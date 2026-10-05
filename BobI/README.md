# BobI — the Bob Index

A small **PyPI-like web index of Lynxer modules**, served by a Node.js service
and backed by a **Supabase** (Postgres) database. It is read-only for now: there
are no uploads, and the Rust `bob` CLI is unchanged — the index is the human
browsable front end for the same data `bob install` resolves against.

## Routes

| Route | What it serves |
| --- | --- |
| `GET /` | Index page: search (`?q=`), sort (`?sort=name\|updated`) and pagination (`?page`, `?pageSize`; default 20, max 100). Lists each module's name, version, description, repository and updated date. |
| `GET /modules/:name` | Module page: breadcrumb, metadata (including the updated date), the GitHub link, and the `bob install <name> <version>` command. |
| `GET /api/modules` | JSON list of the current page — accepts `?q`, `?sort`, `?page`, `?pageSize`. |
| `GET /api/modules/:name` | JSON for one module (`404` when unknown). |
| `GET /health` | `{"status":"ok"}`. |

## Data

Reads the `public.modules` table in Supabase (one row per module):

```
name text primary key, repository text, version text, description text, created_at, updated_at
```

The table allows public `SELECT` (RLS), so the service uses only the public
**anon / publishable** key. The schema and seed live in
[`../supabase/migrations/`](../supabase/migrations).

## Configuration

| Variable | Meaning |
| --- | --- |
| `SUPABASE_URL` | `https://<project-ref>.supabase.co`. |
| `SUPABASE_ANON_KEY` | The project's public anon (or `sb_publishable_...`) key. |
| `PORT` | Port to listen on (Render sets this; defaults to `3000`). |

Copy `.env.example` to `.env` for local development; never commit `.env`.

## Run locally

```console
$ npm install
$ cp .env.example .env      # then fill in SUPABASE_URL / SUPABASE_ANON_KEY
$ npm start
Bob Index listening on http://localhost:3000
```

## Deploy (Render)

The root [`render.yaml`](../render.yaml) Blueprint defines the service
(`rootDir: BobI`, `npm install` / `npm start`, health check `/health`). Create it
once from the Render dashboard (**New → Blueprint → this repo**), set
`SUPABASE_URL` and `SUPABASE_ANON_KEY` in the service's environment, and Render
builds and deploys on every push to `main`. Validate the Blueprint with:

```console
$ render blueprints validate ./render.yaml
```
