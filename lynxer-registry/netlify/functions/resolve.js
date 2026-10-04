// Netlify Function: resolve a Lynxer package name to its GitHub repository.
// Equivalent to the old Express `POST /api/resolve` endpoint (see netlify.toml).
//
// registry.json maps a package name to its GitHub repo and version:
//
//   { "packages": {
//       "math-utils": { "repository": "lynxer-lang/math-utils", "version": "1.2.3" }
//   } }
//
// `repository` is `owner/repo` (a full github.com URL also works); `owner` may
// instead be given as a separate field. `version` is optional.
const config = require('../../registry.json');

const JSON_HEADERS = { 'Content-Type': 'application/json' };

function respond(statusCode, payload) {
  return { statusCode, headers: JSON_HEADERS, body: JSON.stringify(payload) };
}

// Normalise an entry to { owner, repository }, or null when it is unusable.
function splitRepository(entry) {
  if (!entry || !entry.repository) {
    return null;
  }

  let repository = String(entry.repository).trim();
  const marker = 'github.com/';
  if (repository.includes(marker)) {
    repository = repository.split(marker)[1];
  }
  repository = repository.replace(/\.git$/, '').replace(/^\/+/, '');

  const parts = repository.split('/').filter(Boolean);
  if (parts.length >= 2) {
    return { owner: entry.owner || parts[0], repository: parts[1] };
  }
  if (entry.owner && parts.length === 1) {
    return { owner: entry.owner, repository: parts[0] };
  }
  return null;
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

  const entry = config.packages[name];
  if (!entry) {
    return respond(404, { error: `Package '${name}' not found` });
  }

  const resolved = splitRepository(entry);
  if (!resolved) {
    return respond(500, { error: `Package '${name}' has an invalid registry entry` });
  }

  const payload = { repository: resolved.repository, owner: resolved.owner };
  if (entry.version) {
    payload.version = entry.version;
  }
  return respond(200, payload);
};
