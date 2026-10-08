#!/usr/bin/env bash
# EVAL-GUITAR-009 — run the guitar-lock-eval test suite (Python standard library).
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$here"
exec python3 -m unittest discover -s tests -v
