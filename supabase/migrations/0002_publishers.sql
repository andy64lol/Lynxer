-- Bob registry: the publish whitelist.
--
-- Writing to public.modules is restricted to the accounts listed here. Reads
-- stay public (the Bob Index and the resolve API use the anon key); migrations
-- and admin work use the service-role key, which bypasses RLS.

create table if not exists public.publishers (
  email    text primary key,
  note     text,
  added_at timestamptz not null default now()
);

alter table public.publishers enable row level security;

-- The whitelist is not secret, but not public either: only signed-in users may
-- read it (so a publisher can check whether they are allowed).
drop policy if exists "publishers are readable by authenticated users" on public.publishers;
create policy "publishers are readable by authenticated users"
  on public.publishers for select to authenticated using (true);

-- Publishing: an authenticated user whose email is whitelisted may insert,
-- update and delete registry rows. The email claim travels in the JWT.
drop policy if exists "modules are published by whitelisted users" on public.modules;
create policy "modules are published by whitelisted users"
  on public.modules for insert to authenticated
  with check (exists (
    select 1 from public.publishers
    where lower(publishers.email) = lower(auth.jwt() ->> 'email')
  ));

drop policy if exists "modules are updated by whitelisted users" on public.modules;
create policy "modules are updated by whitelisted users"
  on public.modules for update to authenticated
  using (exists (
    select 1 from public.publishers
    where lower(publishers.email) = lower(auth.jwt() ->> 'email')
  ))
  with check (exists (
    select 1 from public.publishers
    where lower(publishers.email) = lower(auth.jwt() ->> 'email')
  ));

drop policy if exists "modules are deleted by whitelisted users" on public.modules;
create policy "modules are deleted by whitelisted users"
  on public.modules for delete to authenticated
  using (exists (
    select 1 from public.publishers
    where lower(publishers.email) = lower(auth.jwt() ->> 'email')
  ));

-- The only publisher for now.
insert into public.publishers (email, note)
values ('andy64lolxd@gmail.com', 'owner')
on conflict (email) do nothing;
