create extension if not exists pgcrypto;

create table if not exists public.alfred_conversations (
  id uuid primary key default gen_random_uuid(),
  user_id uuid not null references auth.users(id) on delete cascade,
  user_text text not null check (char_length(user_text) <= 4000),
  assistant_text text not null check (char_length(assistant_text) <= 8000),
  created_at timestamptz not null default now()
);
create index if not exists alfred_conversations_user_created_idx
  on public.alfred_conversations (user_id, created_at desc);

create table if not exists public.alfred_memories (
  id uuid primary key default gen_random_uuid(),
  user_id uuid not null references auth.users(id) on delete cascade,
  memory_text text not null check (char_length(memory_text) between 1 and 500),
  created_at timestamptz not null default now()
);
create index if not exists alfred_memories_user_created_idx
  on public.alfred_memories (user_id, created_at desc);

alter table public.alfred_conversations enable row level security;
alter table public.alfred_memories enable row level security;
-- Data access is performed only by the Edge Function after verifying the user's JWT.
-- No direct anon/authenticated table policies are created.
revoke all on public.alfred_conversations from anon, authenticated;
revoke all on public.alfred_memories from anon, authenticated;
grant all on public.alfred_conversations to service_role;
grant all on public.alfred_memories to service_role;

create table if not exists public.alfred_usage (
  user_id uuid not null references auth.users(id) on delete cascade,
  usage_day date not null default current_date,
  request_count integer not null default 0 check (request_count >= 0),
  primary key (user_id, usage_day)
);
alter table public.alfred_usage enable row level security;
revoke all on public.alfred_usage from anon, authenticated;
grant all on public.alfred_usage to service_role;

create or replace function public.alfred_take_daily_quota(p_user_id uuid, p_limit integer default 120)
returns boolean
language plpgsql
security definer
set search_path = public
as $$
declare
  new_count integer;
begin
  insert into public.alfred_usage(user_id, usage_day, request_count)
  values (p_user_id, current_date, 1)
  on conflict (user_id, usage_day) do update
    set request_count = public.alfred_usage.request_count + 1
    where public.alfred_usage.request_count < p_limit
  returning request_count into new_count;
  return new_count is not null;
end;
$$;
revoke all on function public.alfred_take_daily_quota(uuid, integer) from public, anon, authenticated;
grant execute on function public.alfred_take_daily_quota(uuid, integer) to service_role;
