-- Bob registry: evaluate the publish whitelist through a SECURITY DEFINER
-- predicate. Policies that look the whitelist up directly are fragile (they run
-- under the caller's RLS); this function runs as its owner, so the lookup is
-- unaffected, and it is the single place the rule lives.

create or replace function public.is_publisher()
returns boolean
language sql
stable
security definer
set search_path = public
as $$
  select exists (
    select 1
    from public.publishers p
    where lower(p.email) = lower(coalesce(auth.jwt() ->> 'email', ''))
  );
$$;

revoke all on function public.is_publisher() from public;
grant execute on function public.is_publisher() to authenticated;

-- Re-point the registry policies at the predicate.
drop policy if exists "modules are published by whitelisted users" on public.modules;
create policy "modules are published by whitelisted users"
  on public.modules for insert to authenticated
  with check (public.is_publisher());

drop policy if exists "modules are updated by whitelisted users" on public.modules;
create policy "modules are updated by whitelisted users"
  on public.modules for update to authenticated
  using (public.is_publisher())
  with check (public.is_publisher());

drop policy if exists "modules are deleted by whitelisted users" on public.modules;
create policy "modules are deleted by whitelisted users"
  on public.modules for delete to authenticated
  using (public.is_publisher());
