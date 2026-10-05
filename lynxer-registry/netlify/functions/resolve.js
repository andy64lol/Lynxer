// Netlify Function: resolve a Lynxer package name to its GitHub repository.
//
// The public API path is POST /api/resolve (see netlify.toml). The registry now
// lives in Supabase (the public.modules table) instead of registry.json; the
// response shape is unchanged, so the bob CLI needs no change:
//
//   { "owner": "...", "repository": "...", "version": "..." }
//
// Requires SUPABASE_URL and SUPABASE_ANON_KEY in the site environment. The
// module table allows public SELECT, so the anon/publishable key is enough.
const JSON_HEADERS = { 'Content-Type': 'application/json' };

function respond(statusCode, payload) {
  return { statusCode, headers: JSON_HEADERS, body: JSON.stringify(payload) };
}

// "owner/repo" (a full github.com URL also works) -> { owner, repository }.
function splitRepository(repository) {
  if (!repository) {
    return null;
  }
  let value = String(repository).trim();
  const marker = 'github.com/';
  if (value.includes(marker)) {
    value = value.split(marker)[1];
  }
  value = value.replace(/\.git$/, '').replace(/^\/+/, '');
  const parts = value.split('/').filter(Boolean);
  if (parts.length >= 2) {
    return { owner: parts[0], repository: parts[1] };
  }
  return null;
}

async function lookupModule(name) {
  const base = process.env.SUPABASE_URL;
  const key = process.env.SUPABASE_ANON_KEY;
  if (!base || !key) {
    throw new Error('SUPABASE_URL and SUPABASE_ANON_KEY are not configured');
  }
  const endpoint =
    `${base.replace(/\/+$/, '')}/rest/v1/modules` +
    `?select=name,repository,version&name=eq.${encodeURIComponent(name)}`;
  const response = await fetch(endpoint, {
    headers: { apikey: key, Authorization: `Bearer ${key}` },
  });
  if (!response.ok) {
    throw new Error(`registry database returned HTTP ${response.status}`);
  }
  const rows = await response.json();
  return Array.isArray(rows) && rows.length > 0 ? rows[0] : null;
}

exports.handler = async (event) => {
  if (event.httpMethod !== 'POST') {
    return respond(405, { error: 'Method not allowed' });
  }

  let body;
  try {
    body = event.body ? JSON.parse(event.body) : {};
  } catch (error) {
    return respond(400, { error: 'Request body must be valid JSON' });
  }

  const name = body && body.name;
  if (!name) {
    return respond(400, { error: 'Package name is required' });
  }

  let row;
  try {
    row = await lookupModule(name);
  } catch (error) {
    return respond(502, { error: String((error && error.message) || error) });
  }
  if (!row) {
    return respond(404, { error: `Package '${name}' not found` });
  }

  const split = splitRepository(row.repository);
  if (!split) {
    return respond(500, { error: `Package '${name}' has an invalid registry entry` });
  }

  const payload = { repository: split.repository, owner: split.owner };
  if (row.version) {
    payload.version = row.version;
  }
  return respond(200, payload);
};
