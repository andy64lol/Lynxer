'use strict';

require('dotenv').config();

const express = require('express');
const { getClient } = require('./lib/supabase');
const { renderIndex, renderModule, renderError } = require('./views/templates');

const app = express();
const PORT = process.env.PORT || 3000;

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
    res.send(renderIndex(await fetchModules(query), query));
  } catch (error) {
    res.status(500).send(renderError(500, `Could not load modules: ${(error && error.message) || error}`));
  }
});

app.get('/modules/:name', async (req, res) => {
  try {
    const mod = await fetchModule(req.params.name);
    if (!mod) {
      return res.status(404).send(renderError(404, `Module '${req.params.name}' not found`));
    }
    res.send(renderModule(mod));
  } catch (error) {
    res.status(500).send(renderError(500, `Could not load the module: ${(error && error.message) || error}`));
  }
});

app.use((_req, res) => res.status(404).send(renderError(404, 'Page not found')));

app.listen(PORT, () => {
  console.log(`Bob Index listening on http://localhost:${PORT}`);
});
