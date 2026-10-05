-- Bob registry: the module index.
--
-- One row per module. Reads are public (RLS policy below); writes happen
-- through migrations or a service-role key, never an anonymous client.

create table if not exists public.modules (
  name        text primary key,
  repository  text not null,              -- GitHub repository, owner/repo
  version     text,                       -- latest published version
  description text,
  created_at  timestamptz not null default now(),
  updated_at  timestamptz not null default now()
);

alter table public.modules enable row level security;

drop policy if exists "modules are public" on public.modules;
create policy "modules are public"
  on public.modules for select using (true);

-- Seed with the existing registry contents.
insert into public.modules (name, repository, version, description)
values ('foo', 'andy64lol/foo', '0.1.0', 'An example Lynxer module that says foo bar!')
on conflict (name) do nothing;
