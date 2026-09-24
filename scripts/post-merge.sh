#!/usr/bin/env bash
set -euo pipefail

# Post-merge setup runs from the workspace root with stdin closed.
# Keep all project-relative work inside the Lynxer checkout.
cd "$(dirname "${BASH_SOURCE[0]}")/.."

# Build the standalone C++ interpreter and its native (C++ and Rust) stdlib
# modules. `cargo` is optional; the build skips the Rust backends without it.
make buildLynxer
