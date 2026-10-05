'use strict';

function escapeHtml(value) {
  return String(value == null ? '' : value)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

const STYLE = `
  :root { color-scheme: light; }
  * { box-sizing: border-box; }
  body { margin: 0; font: 16px/1.55 -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; color: #212529; background: #fff; }
  a { color: #0b6bcb; }
  header { background: #f8f9fa; border-bottom: 1px solid #e3e6e8; padding: 1rem 1.5rem; }
  header .wrap, main, footer { max-width: 60rem; margin: 0 auto; }
  .header-inner { display: flex; align-items: baseline; justify-content: space-between; gap: 1rem; flex-wrap: wrap; }
  h1 { font-size: 1.4rem; margin: 0; }
  h1 a { color: #212529; text-decoration: none; }
  h1 .sub { color: #6c757d; font-weight: 400; font-size: 1rem; }
  .account-nav { font-size: .95rem; }
  .account-nav a { margin-left: 1rem; text-decoration: none; }
  .account-nav a:hover { text-decoration: underline; }
  main { padding: 1.5rem; }
  form.search { display: flex; gap: .5rem; margin: 1.25rem 0; }
  input[type=search], input[type=email], input[type=password] { padding: .55rem .7rem; font-size: 1rem; border: 1px solid #ced4da; border-radius: 6px; font: inherit; }
  input[type=search] { flex: 1; }
  button { padding: .55rem 1rem; font-size: 1rem; border: 1px solid #0b6bcb; background: #0b6bcb; color: #fff; border-radius: 6px; cursor: pointer; }
  button:hover { background: #0a5cb0; }
  ul.packages { list-style: none; margin: 0; padding: 0; }
  ul.packages li { padding: .9rem 0; border-bottom: 1px solid #eceff1; }
  .pkg-name { font-size: 1.05rem; font-weight: 600; text-decoration: none; }
  .pkg-version { color: #6c757d; font-size: .9rem; margin-left: .4rem; }
  .pkg-desc { color: #495057; margin: .25rem 0 0; }
  .pkg-repo { color: #6c757d; font-size: .85rem; }
  .muted { color: #6c757d; }
  pre { background: #f6f8fa; border: 1px solid #e3e6e8; border-radius: 6px; padding: .75rem 1rem; overflow-x: auto; }
  table { border-collapse: collapse; }
  th, td { text-align: left; padding: .35rem .9rem .35rem 0; vertical-align: top; }
  .auth-form { display: flex; flex-direction: column; gap: .9rem; max-width: 24rem; margin: 1.25rem 0; }
  .auth-form label { display: flex; flex-direction: column; gap: .3rem; font-weight: 600; font-size: .95rem; }
  .auth-form input { font-weight: 400; }
  .auth-message { margin: .75rem 0; }
  .auth-message.error { color: #b3261e; }
  .auth-message.ok { color: #1a7f37; }
  footer { padding: 1.5rem; color: #6c757d; font-size: .85rem; }
`;

function layout(title, body, config = {}, page = '') {
  const injected = {
    url: config.url || '',
    anonKey: config.anonKey || '',
  };
  return `<!doctype html>
<html lang="en">
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>${escapeHtml(title)}</title><style>${STYLE}</style></head>
<body data-page="${escapeHtml(page)}">
<header><div class="wrap header-inner"><h1><a href="/">Bob Index</a> <span class="sub">Lynxer package index</span></h1><nav class="account-nav" id="account-nav"><a href="/signup">Sign up</a><a href="/login">Log in</a></nav></div></header>
<main>${body}</main>
<footer>Bob Index — the module index for the Lynxer package manager.</footer>
<script>window.BOB_SUPABASE = ${JSON.stringify(injected)};</script>
<script type="module" src="/auth.js"></script>
</body></html>`;
}

function renderIndex(modules, query, config) {
  const rows = modules
    .map((m) => {
      const version = m.version ? `<span class="pkg-version">${escapeHtml(m.version)}</span>` : '';
      const repo = m.repository
        ? ` <a class="pkg-repo" href="https://github.com/${escapeHtml(m.repository)}">${escapeHtml(m.repository)}</a>`
        : '';
      const description = m.description ? `<p class="pkg-desc">${escapeHtml(m.description)}</p>` : '';
      return `<li><div><a class="pkg-name" href="/modules/${encodeURIComponent(m.name)}">${escapeHtml(m.name)}</a>${version}${repo}</div>${description}</li>`;
    })
    .join('');

  const body = `
    <form class="search" method="get" action="/">
      <input type="search" name="q" value="${escapeHtml(query || '')}" placeholder="Search modules" aria-label="Search modules">
      <button type="submit">Search</button>
    </form>
    <p class="muted">${modules.length} module${modules.length === 1 ? '' : 's'}${query ? ` matching &ldquo;${escapeHtml(query)}&rdquo;` : ''}</p>
    <ul class="packages">${rows || '<li class="muted">No modules found.</li>'}</ul>`;
  return layout('Bob Index', body, config, 'index');
}

function renderModule(mod, config) {
  const repoUrl = mod.repository ? `https://github.com/${mod.repository}` : null;
  const install = `bob install ${mod.name}${mod.version ? ` ${mod.version}` : ''}`;
  const body = `
    <h2>${escapeHtml(mod.name)}${mod.version ? ` <span class="pkg-version">${escapeHtml(mod.version)}</span>` : ''}</h2>
    ${mod.description ? `<p>${escapeHtml(mod.description)}</p>` : ''}
    <table>
      <tr><th>Name</th><td>${escapeHtml(mod.name)}</td></tr>
      ${mod.version ? `<tr><th>Version</th><td>${escapeHtml(mod.version)}</td></tr>` : ''}
      ${repoUrl ? `<tr><th>Repository</th><td><a href="${repoUrl}">${escapeHtml(mod.repository)}</a></td></tr>` : ''}
    </table>
    <h3>Install</h3>
    <pre>${escapeHtml(install)}</pre>
    <p class="muted"><a href="/">&larr; Back to the index</a></p>`;
  return layout(`${mod.name} · Bob Index`, body, config, 'module');
}

function renderError(status, message, config) {
  return layout(
    `${status} · Bob Index`,
    `<h2>${status}</h2><p>${escapeHtml(message)}</p><p class="muted"><a href="/">&larr; Back to the index</a></p>`,
    config,
    'error'
  );
}

function renderSignup(config) {
  const body = `
    <h2>Create an account</h2>
    <p class="muted">Sign up for the Bob Index. We'll email you a link to confirm your address.</p>
    <form id="signup-form" class="auth-form">
      <label>Email<input type="email" name="email" autocomplete="email" required></label>
      <label>Password<input type="password" name="password" autocomplete="new-password" minlength="6" required></label>
      <button type="submit">Create account</button>
    </form>
    <p id="auth-message" class="auth-message" role="status"></p>
    <p class="muted">Already have an account? <a href="/login">Log in</a>.</p>`;
  return layout('Sign up · Bob Index', body, config, 'signup');
}

function renderLogin(config) {
  const body = `
    <h2>Log in</h2>
    <form id="login-form" class="auth-form">
      <label>Email<input type="email" name="email" autocomplete="email" required></label>
      <label>Password<input type="password" name="password" autocomplete="current-password" required></label>
      <button type="submit">Log in</button>
    </form>
    <p id="auth-message" class="auth-message" role="status"></p>
    <p class="muted">No account yet? <a href="/signup">Sign up</a>. <a href="/forgot-password">Forgot password?</a></p>`;
  return layout('Log in · Bob Index', body, config, 'login');
}

function renderAccount(config) {
  const body = `
    <h2>Your account</h2>
    <div id="account"><p class="muted">Loading…</p></div>`;
  return layout('Account · Bob Index', body, config, 'account');
}

function renderConfirm(config) {
  const body = `
    <h2>Confirming your email</h2>
    <p id="auth-message" class="auth-message" role="status">Confirming…</p>`;
  return layout('Confirm · Bob Index', body, config, 'confirm');
}

function renderForgotPassword(config) {
  const body = `
    <h2>Reset your password</h2>
    <p class="muted">Enter your email and we'll send a link to set a new password.</p>
    <form id="forgot-form" class="auth-form">
      <label>Email<input type="email" name="email" autocomplete="email" required></label>
      <button type="submit">Send reset link</button>
    </form>
    <p id="auth-message" class="auth-message" role="status"></p>
    <p class="muted"><a href="/login">Back to log in</a></p>`;
  return layout('Reset password · Bob Index', body, config, 'forgot');
}

function renderResetPassword(config) {
  const body = `
    <h2>Set a new password</h2>
    <form id="reset-form" class="auth-form">
      <label>New password<input type="password" name="password" autocomplete="new-password" minlength="6" required></label>
      <button type="submit">Update password</button>
    </form>
    <p id="auth-message" class="auth-message" role="status"></p>`;
  return layout('Set a new password · Bob Index', body, config, 'reset');
}

module.exports = {
  escapeHtml,
  layout,
  renderIndex,
  renderModule,
  renderError,
  renderSignup,
  renderLogin,
  renderAccount,
  renderConfirm,
  renderForgotPassword,
  renderResetPassword,
};
