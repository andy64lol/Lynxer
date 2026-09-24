#!/usr/bin/env bash
set -euo pipefail

# Post-merge setup runs from the workspace root with stdin closed.
# Keep all project-relative work inside the Lynxer checkout.
cd "$(dirname "${BASH_SOURCE[0]}")/.."

# The lite dependency set provides the compiler/build tools and syscall tables
# needed by the native extensions without pulling in optional GUI backends.
make liteDeps
make buildCpp