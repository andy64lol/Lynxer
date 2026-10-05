'use strict';

const { createClient } = require('@supabase/supabase-js');

let client = null;

// A single read-only client, created from the environment. The public
// (anon/publishable) key is enough because the `modules` table allows public
// SELECT; writes go through migrations, never through this service.
function getClient() {
  if (client) {
    return client;
  }
  const url = process.env.SUPABASE_URL;
  const key = process.env.SUPABASE_ANON_KEY || process.env.SUPABASE_KEY;
  if (!url || !key) {
    throw new Error('SUPABASE_URL and SUPABASE_ANON_KEY must be set');
  }
  client = createClient(url, key, { auth: { persistSession: false } });
  return client;
}

module.exports = { getClient };
