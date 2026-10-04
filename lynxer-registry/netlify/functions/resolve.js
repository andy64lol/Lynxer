// Netlify Function: resolve a Lynxer package name to its GitHub repository.
// Equivalent to the old Express `POST /api/resolve` endpoint (see netlify.toml).
const config = require('../../registry.json');

const JSON_HEADERS = { 'Content-Type': 'application/json' };

function respond(statusCode, payload) {
  return { statusCode, headers: JSON_HEADERS, body: JSON.stringify(payload) };
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

  const packageInfo = config.packages[name];
  if (!packageInfo) {
    return respond(404, { error: `Package '${name}' not found` });
  }

  return respond(200, {
    repository: packageInfo.repository,
    owner: packageInfo.owner,
  });
};
