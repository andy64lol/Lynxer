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

// The public Supabase settings are injected into the browser for auth.js; the
// anon key is a public client key, safe to expose.
const authConfig = {
  url: process.env.SUPABASE_URL || '',
  anonKey: process.env.SUPABASE_ANON_KEY || '',
};

const COLUMNS = 'name,repository,version,description';

async function fetchModules(query) {
  const { data, error } = await getClient()
    .from('modules')
    .select(COLUMNS)
    .order('name', { ascending: true });
  if (error) {
    throw error;
  }
  if (!query) {
    return data;
  }
  const needle = query.toLowerCase();
  return data.filter(
    (m) =>
      (m.name || '').toLowerCase().includes(needle) ||
      (m.description || '').toLowerCase().includes(needle)
  );
}

async function fetchModule(name) {
  const { data, error } = await getClient()
    .from('modules')
    .select(COLUMNS)
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
    res.json(await fetchModules((req.query.q || '').trim()));
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
  const query = (req.query.q || '').trim();
  try {
    res.send(renderIndex(await fetchModules(query), query, authConfig));
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
