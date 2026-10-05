# Bob Index accounts

The Bob Index has optional user accounts built on **Supabase Auth**, with
**email verification** delivered by **Resend**. The index is still **read-only** —
accounts are groundwork (no uploads or per-module permissions yet).

## Routes

| Route | Page |
| --- | --- |
| `GET /signup` | Create an account (email + password). |
| `GET /login` | Sign in. |
| `GET /account` | The signed-in user's details. |
| `GET /auth/confirm` | Landing page for the email-confirmation link. |
| `GET /forgot-password` | Request a password-reset link. |
| `GET /auth/reset-password` | Set a new password (arrived from the reset link). |
| `GET /auth.js` | The browser script that drives the above. |

The header shows Sign up / Log in, or the signed-in email plus Sign out.

## How it works

1. `/signup` calls Supabase `signUp` with `emailRedirectTo = <origin>/auth/confirm`.
2. Supabase Auth sends a "Confirm your email address" message through its custom
   SMTP — which is **Resend** (`smtp.resend.com`, user `resend`).
3. The link returns to `/auth/confirm`; the browser session is established and
   `/account` shows the user.

Sessions live in the browser (`localStorage`) via `@supabase/supabase-js`, and
sign-in requires a confirmed address (`auth.email.enable_confirmations`).

### Password reset

`/forgot-password` calls `resetPasswordForEmail` with
`redirectTo = <origin>/auth/reset-password`; Supabase emails a recovery link
(again through Resend SMTP), which lands on `/auth/reset-password` where the
user sets a new password with `updateUser({ password })`. The log-in page links
to it under "Forgot password?".

## Configuration

Supabase Auth settings live in [`../supabase/config.toml`](../supabase/config.toml)
and are applied with the Supabase CLI (no dashboard needed):

```console
$ supabase config diff                     # preview the changes
$ RESEND_API_KEY=re_... supabase config push --yes
```

The relevant keys:

```toml
[auth]
site_url = "https://bobi-index.onrender.com"
additional_redirect_urls = ["https://bobi-index.onrender.com/**", "http://localhost:3000/**"]

[auth.email]
enable_confirmations = true

[auth.email.smtp]
enabled = true
host = "smtp.resend.com"
port = 587
user = "resend"
pass = "env(RESEND_API_KEY)"     # the secret stays out of the file
admin_email = "onboarding@resend.dev"
sender_name = "Bob Index"
```

## Resend

The SMTP password is a Resend API key. Create a sending-only key with the Resend
CLI and test delivery:

```console
$ resend login
$ resend api-keys create --name supabase-smtp --permission sending_access
$ resend emails send --from onboarding@resend.dev --to you@example.com \
    --subject test --text hello
$ resend emails list            # delivery status
```

> **Sender domain.** No sending domain is verified yet, so **both** the
> confirmation and password-reset emails are sent from `onboarding@resend.dev`,
> which reliably reaches only the Resend account owner. Verify a domain
> (`resend domains create ...`) before real sign-ups.

## Security

- The Resend API key is a secret: it is referenced as `env(RESEND_API_KEY)` in
  `config.toml`, never committed.
- `SUPABASE_ANON_KEY` is a public client key (safe in the browser); the
  service-role key is not used by the index.
- Sessions use `localStorage`; because the index is read-only this is not
  server-enforced authorization.

## See also

- [bob-index.md](bob-index.md) — the index pages
- [bob-registry.md](bob-registry.md) — the resolve API and database
