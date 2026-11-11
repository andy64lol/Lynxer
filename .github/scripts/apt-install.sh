#!/usr/bin/env bash
# Install Ubuntu packages with bounded retries.
#
# GitHub-hosted Ubuntu mirrors occasionally fail DNS resolution or stall for
# tens of minutes. Refresh the package lists best-effort, then install with a
# hard timeout so a broken mirror fails the step quickly instead of burning
# the job's entire timeout budget.
set -euo pipefail

if [ "$#" -eq 0 ]; then
  echo "usage: $0 <package> [package ...]" >&2
  exit 2
fi

apt_opts=(
  -o Acquire::Retries=3
  -o Acquire::ForceIPv4=true
  -o Acquire::http::Timeout=20
  -o Acquire::https::Timeout=20
  -o DPkg::Lock::Timeout=60
)

# A failed refresh is not fatal: the runner image ships cached package lists
# and the install below can still succeed from them.
if ! sudo DEBIAN_FRONTEND=noninteractive timeout 150 apt-get "${apt_opts[@]}" update; then
  echo "::warning::apt-get update failed; continuing with the runner's cached package lists."
fi

for attempt in 1 2; do
  if sudo DEBIAN_FRONTEND=noninteractive timeout 240 apt-get "${apt_opts[@]}" install -y --no-install-recommends "$@"; then
    exit 0
  fi
  if [ "$attempt" -lt 2 ]; then
    echo "::warning::apt-get install failed (attempt ${attempt}); retrying in 5s."
    sleep 5
  fi
done

exit 1
