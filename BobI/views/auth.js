// Browser-side Supabase Auth for the Bob Index.
//
// Loaded as a module on every page (see views/templates.js). The Supabase URL
// and anon key are injected as window.BOB_SUPABASE (both are public). Sessions
// live in the browser (localStorage); the index itself stays read-only.
import { createClient } from 'https://esm.sh/@supabase/supabase-js@2';

const cfg = window.BOB_SUPABASE || {};
const page = document.body.getAttribute('data-page') || '';
const message = document.getElementById('auth-message');

function setMessage(text, kind) {
  if (!message) {
    return;
  }
  message.textContent = text;
  message.className = 'auth-message ' + (kind || '');
}

function escapeHtml(value) {
  return String(value == null ? '' : value).replace(/[&<>"']/g, (c) =>
    ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c])
  );
}

let supabase = null;
if (cfg.url && cfg.anonKey) {
  supabase = createClient(cfg.url, cfg.anonKey, {
    auth: { persistSession: true, autoRefreshToken: true, detectSessionInUrl: true },
  });
} else {
  setMessage('Accounts are unavailable: the server is missing Supabase configuration.', 'error');
}

async function session() {
  if (!supabase) {
    return null;
  }
  const { data } = await supabase.auth.getSession();
  return data.session || null;
}

function renderNav(current) {
  const nav = document.getElementById('account-nav');
  if (!nav) {
    return;
  }
  if (current && current.user) {
    nav.innerHTML =
      `<a href="/account">${escapeHtml(current.user.email || 'account')}</a>` +
      '<a href="#" id="nav-signout">Sign out</a>';
    const out = document.getElementById('nav-signout');
    if (out) {
      out.addEventListener('click', async (event) => {
        event.preventDefault();
        await supabase.auth.signOut();
        window.location.href = '/';
      });
    }
  } else {
    nav.innerHTML = '<a href="/signup">Sign up</a><a href="/login">Log in</a>';
  }
}

if (page === 'signup' && supabase) {
  document.getElementById('signup-form').addEventListener('submit', async (event) => {
    event.preventDefault();
    const form = event.target;
    setMessage('Creating your account…', '');
    const { error } = await supabase.auth.signUp({
      email: form.email.value.trim(),
      password: form.password.value,
      options: { emailRedirectTo: window.location.origin + '/auth/confirm' },
    });
    if (error) {
      setMessage(error.message, 'error');
    } else {
      setMessage(
        'Account created. Check ' + form.email.value.trim() + ' for a confirmation link.',
        'ok'
      );
    }
  });
}

if (page === 'login' && supabase) {
  document.getElementById('login-form').addEventListener('submit', async (event) => {
    event.preventDefault();
    const form = event.target;
    setMessage('Signing in…', '');
    const { error } = await supabase.auth.signInWithPassword({
      email: form.email.value.trim(),
      password: form.password.value,
    });
    if (error) {
      setMessage(error.message, 'error');
    } else {
      window.location.href = '/account';
    }
  });
}

if (page === 'account' && supabase) {
  const box = document.getElementById('account');
  (async () => {
    const current = await session();
    if (!current) {
      box.innerHTML =
        '<p>You are not signed in. <a href="/login">Log in</a> or ' +
        '<a href="/signup">sign up</a>.</p>';
      return;
    }
    const user = current.user;
    box.innerHTML =
      '<table>' +
      `<tr><th>Email</th><td>${escapeHtml(user.email || '')}</td></tr>` +
      `<tr><th>Email confirmed</th><td>${user.email_confirmed_at ? 'yes' : 'no'}</td></tr>` +
      `<tr><th>User ID</th><td><code>${escapeHtml(user.id)}</code></td></tr>` +
      `<tr><th>Created</th><td>${escapeHtml(user.created_at || '')}</td></tr>` +
      '</table>' +
      '<p><button id="account-signout">Sign out</button></p>';
    document.getElementById('account-signout').addEventListener('click', async () => {
      await supabase.auth.signOut();
      window.location.href = '/';
    });
  })();
}

if (page === 'confirm' && supabase) {
  // detectSessionInUrl consumes the confirmation link's tokens and creates the
  // session; onAuthStateChange fires once that has happened.
  supabase.auth.onAuthStateChange((event, current) => {
    if (current && current.user) {
      setMessage('Email confirmed — you are signed in as ' + (current.user.email || '') + '.', 'ok');
      if (message && !document.getElementById('confirm-next')) {
        const link = document.createElement('p');
        link.id = 'confirm-next';
        link.innerHTML = '<a href="/account">Go to your account &rarr;</a>';
        message.after(link);
      }
    }
  });
  session().then((current) => {
    if (!current) {
      setMessage('This confirmation link is invalid or has expired. Try signing up again.', 'error');
    }
  });
}

session().then(renderNav);
if (supabase) {
  supabase.auth.onAuthStateChange((_event, current) => renderNav(current));
}
