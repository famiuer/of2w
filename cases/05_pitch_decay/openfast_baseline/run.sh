#!/usr/bin/env bash
# Regenerate the OpenFAST pitch-decay baseline (optional — minimal.outb
# ships precomputed). Uses the standalone `openfast` binary built from
# the pinned submodule (external/openfast).
OF=${OPENFAST_BIN:-openfast}
command -v "$OF" >/dev/null || { echo "ERROR: set OPENFAST_BIN or put openfast in PATH"; exit 1; }
cd "$(dirname "$0")"
"$OF" minimal.fst 2>&1 | tee log.openfast
