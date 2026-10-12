#!/bin/sh
case "$1" in
  *Username*) printf '%s\n' 'x-access-token' ;;
  *) printf '%s' "$ANDY_REPO_TOKEN" ;;
esac
