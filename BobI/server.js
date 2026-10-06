'use strict';

require('dotenv').config();

const path = require('path');
const express = require('express');
const { getClient } = require('./lib/supabase');
const {
  renderIndex,
  renderModule,
  renderError,
  renderSignup,
  renderLogin,
  renderAccount,
  renderConfirm,
  renderForgotPassword,
  renderResetPassword,
} = require('./views/templates');

const app = express();
const PORT = process.env.PORT || 3000;

// Static assets (the Lynxer icon) live in BobI/assets so the Render service,
// whose root directory is BobI, stays self-contained.
app.use(express.static(path.join(__dirname, 'assets')));

// The public Supabase settings are injected into the browser for auth.js; the
// anon key is a public client key, safe to expose.
const authConfig = {
  url: process.env.SUPABASE_URL || '',
  anonKey: process.env.SUPABASE_ANON_KEY || '',
};

const COLUMNS = 'name,repository,version,description';
const SELECT = COLUMNS + ',updated_at';
const DEFAULT_PAGE_SIZE = 20;
const MAX_PAGE_SIZE = 100;

// Parse and clamp the list query string.
function normalizeParams(query) {
  const clampInt = (value, fallback, min, max) => {
    const parsed = parseInt(value, 10);
    return Number.isFinite(parsed) ? Math.min(Math.max(parsed, min), max) : fallback;
  };
  return {
    q: String(query.q || '').trim(),
    sort: query.sort === 'updated' ? 'updated' : 'name',
    page: clampInt(query.page, 1, 1, 1000000),
    pageSize: clampInt(query.pageSize, DEFAULT_PAGE_SIZE, 1, MAX_PAGE_SIZE),
  };
}

// `,`, `(`, `)` would break PostgREST's `.or()` grammar; `%`, `_` and `\\` are
// LIKE wildcards, so escape them for a literal match.
function searchTerm(value) {
  return value.replace(/[(),]/g, ' ').replace(/[%_\\]/g, '\\$&');
}

// Returns { rows, total, totalPages, page } for the requested (clamped) page.
async function fetchModules({ q, sort, page, pageSize }) {
  const client = getClient();
  const term = q ? searchTerm(q) : '';

  // Count the matches first: PostgREST answers an offset past the end with a
  // 416 ("Requested range not satisfiable"), so the page must be clamped before
  // the ranged query runs.
  let countQuery = client.from('modules').select('name', { count: 'exact', head: true });
  if (term) {
    countQuery = countQuery.or(`name.ilike.%${term}%,description.ilike.%${term}%`);
  }
  const { count, error: countError } = await countQuery;
  if (countError) {
    throw countError;
  }
  const total = count || 0;
  const totalPages = Math.max(1, Math.ceil(total / pageSize));
  const safePage = Math.min(page, totalPages);
  const from = (safePage - 1) * pageSize;
  const to = from + pageSize - 1;

  let builder = client.from('modules').select(SELECT);
  if (term) {
    builder = builder.or(`name.ilike.%${term}%,description.ilike.%${term}%`);
  }
  builder =
    sort === 'updated'
      ? builder.order('updated_at', { ascending: false }).order('name', { ascending: true })
      : builder.order('name', { ascending: true });

  const { data, error } = await builder.range(from, to);
  if (error) {
    throw error;
  }
  return { rows: data || [], total, totalPages, page: safePage };
}

async function fetchModule(name) {
  const { data, error } = await getClient()
    .from('modules')
    .select(SELECT)
    .eq('name', name)
    .maybeSingle();
  if (error) {
    throw error;
  }
  return data;
}

app.get('/health', (_req, res) => res.json({ status: 'ok' }));

// Account pages (client-side Supabase Auth; see views/auth.js).
app.get('/auth.js', (_req, res) => {
  res.type('application/javascript').sendFile(path.join(__dirname, 'views', 'auth.js'));
});
app.get('/signup', (_req, res) => res.send(renderSignup(authConfig)));
app.get('/login', (_req, res) => res.send(renderLogin(authConfig)));
app.get('/account', (_req, res) => res.send(renderAccount(authConfig)));
app.get('/auth/confirm', (_req, res) => res.send(renderConfirm(authConfig)));
app.get('/forgot-password', (_req, res) => res.send(renderForgotPassword(authConfig)));
app.get('/auth/reset-password', (_req, res) => res.send(renderResetPassword(authConfig)));

app.get('/api/modules', async (req, res) => {
  try {
    const { rows } = await fetchModules(normalizeParams(req.query));
    res.json(rows);
  } catch (error) {
    res.status(500).json({ error: String((error && error.message) || error) });
  }
});

app.get('/api/modules/:name', async (req, res) => {
  try {
    const mod = await fetchModule(req.params.name);
    if (!mod) {
      return res.status(404).json({ error: `Module '${req.params.name}' not found` });
    }
    res.json(mod);
  } catch (error) {
    res.status(500).json({ error: String((error && error.message) || error) });
  }
});

app.get('/', async (req, res) => {
  const params = normalizeParams(req.query);
  try {
    const result = await fetchModules(params);
    res.send(renderIndex({ ...params, ...result }, authConfig));
  } catch (error) {
    res.status(500).send(renderError(500, `Could not load modules: ${(error && error.message) || error}`, authConfig));
  }
});

app.get('/modules/:name', async (req, res) => {
  try {
    const mod = await fetchModule(req.params.name);
    if (!mod) {
      return res.status(404).send(renderError(404, `Module '${req.params.name}' not found`, authConfig));
    }
    res.send(renderModule(mod, authConfig));
  } catch (error) {
    res.status(500).send(renderError(500, `Could not load the module: ${(error && error.message) || error}`, authConfig));
  }
});

app.use((_req, res) => res.status(404).send(renderError(404, 'Page not found', authConfig)));

app.listen(PORT, () => {
  console.log(`Bob Index listening on http://localhost:${PORT}`);
});
