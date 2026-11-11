#!/usr/bin/env bash
# Install Ubuntu packages with bounded retries and a mirror fallback.
#
# GitHub-hosted runners use a mirrorlist whose entries all live under
# ubuntu.com. When DNS for that zone breaks, apt spends tens of minutes
# retrying and the job fails. Probe the runner's mirrors first, then switch
# to a mirror on a different domain before giving up. Every apt call is
# bounded so a broken mirror cannot burn the job's whole timeout budget.
set -euo pipefail

if [ "$#" -eq 0 ]; then
  echo "usage: $0 <package> [package ...]" >&2
  exit 2
fi

arch="$(dpkg --print-architecture)"
case "$arch" in
  amd64)
    fallback="https://mirrors.ocf.berkeley.edu/ubuntu"
    mirror_hosts=(azure.archive.ubuntu.com archive.ubuntu.com security.ubuntu.com)
    ;;
  arm64)
    fallback="https://mirrors.ocf.berkeley.edu/ubuntu-ports"
    mirror_hosts=(ports.ubuntu.com)
    ;;
  *)
    fallback=""
    mirror_hosts=()
    ;;
esac

apt_opts=(
  -o Acquire::Retries=3
  -o Acquire::ForceIPv4=true
  -o Acquire::http::Timeout=20
  -o Acquire::https::Timeout=20
  -o DPkg::Lock::Timeout=60
)
# Extra options that point apt at the fallback sources file. Empty until the
# default ubuntu.com mirrors fail.
source_opts=()

apt_update() {
  local limit="${1:-150}"
  sudo DEBIAN_FRONTEND=noninteractive timeout "$limit" \
    apt-get "${apt_opts[@]}" "${source_opts[@]}" update
}

apt_install() {
  sudo DEBIAN_FRONTEND=noninteractive timeout 240 \
    apt-get "${apt_opts[@]}" "${source_opts[@]}" install -y --no-install-recommends "$@"
}

write_fallback_sources() {
  local base="$1" codename="$2"
  local file=/etc/apt/sources.list.d/lynxer-fallback.sources

  sudo tee "$file" >/dev/null <<EOF
Types: deb
URIs: ${base}/
Suites: ${codename} ${codename}-updates ${codename}-backports ${codename}-security
Components: main restricted universe multiverse
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
EOF

  # Read only this file and skip the runner's mirrorlist-based sources, which
  # are the ones that keep resolving ubuntu.com hostnames.
  source_opts=(-o Dir::Etc::sourcelist="$file" -o Dir::Etc::sourceparts=-)
}

# If none of the runner's mirror hostnames resolve, skip the doomed refresh
# and go straight to the fallback. This turns a 150s timeout into ~10s.
default_mirrors_resolve() {
  local host
  for host in "${mirror_hosts[@]}"; do
    if timeout 10 getent ahostsv4 "$host" >/dev/null 2>&1; then
      return 0
    fi
  done
  return 1
}

if ! default_mirrors_resolve || ! apt_update 90; then
  echo "::warning::ubuntu.com mirrors are unreachable; trying a fallback mirror."

  codename="$(. /etc/os-release && printf '%s' "${VERSION_CODENAME:-}")"
  if [ -z "$fallback" ]; then
    echo "::warning::No fallback mirror for arch '${arch:-unknown}'; continuing with cached package lists."
  elif [ -z "$codename" ]; then
    echo "::warning::Cannot determine the Ubuntu codename; continuing with cached package lists."
  else
    write_fallback_sources "$fallback" "$codename"
    if ! apt_update 150; then
      echo "::warning::Fallback apt-get update also failed; continuing with cached package lists."
    fi
  fi
fi

for attempt in 1 2; do
  if apt_install "$@"; then
    exit 0
  fi
  if [ "$attempt" -lt 2 ]; then
    echo "::warning::apt-get install failed (attempt ${attempt}); retrying in 5s."
    sleep 5
  fi
done

exit 1
