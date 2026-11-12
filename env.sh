#!/usr/bin/env bash
# env.sh — set up the complete OF2W build/run environment in the CURRENT shell.
#
#   source env.sh                                  # auto-detect OpenFOAM
#   source env.sh /path/to/openfoam-2306/etc/bashrc
#
# Idempotent: safe to source repeatedly, and inside slurm batch scripts
# AFTER any `module load` lines (see INSTALL.md "HPC notes"). Ends with a
# status summary; [WARN]/[FAIL] lines never abort your shell.
#
# Overridable before sourcing (defaults are the in-repo build layout):
#   OF2W_OPENFOAM_BASHRC   path to OpenFOAM etc/bashrc
#   OPENFAST_BUILD_DIR     default: <repo>/build/openfast
#   OPENFAST_SRC_DIR       default: <repo>/external/openfast
#   MOORDYN_INSTALL_DIR    default: <repo>/build/moordyn/install

OF2W_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export OF2W_ROOT
_OF2W_WARN=0; _OF2W_FAIL=0
_of2w_ok()   { printf '  [ OK ] %s\n' "$1"; }
_of2w_warn() { printf '  [WARN] %s\n' "$1"; _OF2W_WARN=$((_OF2W_WARN+1)); }
_of2w_fail() { printf '  [FAIL] %s\n' "$1"; _OF2W_FAIL=$((_OF2W_FAIL+1)); }

# ---- 1 · OpenFOAM --------------------------------------------------------
if [ -z "${WM_PROJECT_DIR:-}" ]; then
    for _c in "${1:-}" "${OF2W_OPENFOAM_BASHRC:-}" \
              /usr/lib/openfoam/openfoam2306/etc/bashrc \
              "$HOME/OpenFOAM/OpenFOAM-v2306/etc/bashrc"; do
        if [ -n "$_c" ] && [ -f "$_c" ]; then . "$_c"; break; fi
    done
fi
if [ -n "${WM_PROJECT_DIR:-}" ] && command -v wmake >/dev/null 2>&1; then
    _of2w_ok "OpenFOAM ${WM_PROJECT_VERSION:-?} ($WM_PROJECT_DIR)"
else
    _of2w_fail "OpenFOAM env not loaded — source env.sh with the bashrc path (see INSTALL.md 'Locating OpenFOAM's bashrc')"
fi

# ---- 2 · OF2W component locations ---------------------------------------
export OPENFAST_BUILD_DIR="${OPENFAST_BUILD_DIR:-$OF2W_ROOT/build/openfast}"
export OPENFAST_SRC_DIR="${OPENFAST_SRC_DIR:-$OF2W_ROOT/external/openfast}"
export MOORDYN_INSTALL_DIR="${MOORDYN_INSTALL_DIR:-$OF2W_ROOT/build/moordyn/install}"

# runtime library resolution (libOF2.so -> libopenfastlib.so, moordyn)
_of2w_ldadd() { case ":${LD_LIBRARY_PATH:-}:" in *":$1:"*) ;; *)
    [ -d "$1" ] && export LD_LIBRARY_PATH="$1${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}";; esac; }
_of2w_ldadd "$OPENFAST_BUILD_DIR/modules/openfast-library"
_of2w_ldadd "$MOORDYN_INSTALL_DIR/lib"
_of2w_ldadd "$MOORDYN_INSTALL_DIR/lib64"

# ---- 3 · waves2Foam (needed by cases 02/03/04/06) ------------------------
if [ -f "$OF2W_ROOT/external/waves2Foam/bin/bashrc" ]; then
    . "$OF2W_ROOT/external/waves2Foam/bin/bashrc" quiet >/dev/null 2>&1
    _of2w_ok "waves2Foam env (WAVES_DIR=$WAVES_DIR)"
else
    _of2w_warn "waves2Foam not set up yet (external/waves2Foam/bin/bashrc missing — run ./bootstrap.sh or see INSTALL.md §7; only needed for the wave cases)"
fi

# ---- 4 · doctor: is everything actually built? ---------------------------
_of2w_lib() {  # $1 lib name, $2 severity(fail|warn), $3 hint
    if [ -n "${FOAM_USER_LIBBIN:-}" ] && [ -f "$FOAM_USER_LIBBIN/$1" ]; then
        _of2w_ok "$1"
    elif [ "$2" = fail ]; then _of2w_fail "$1 missing — $3"
    else _of2w_warn "$1 missing — $3"; fi
}
if [ -n "${WM_PROJECT_DIR:-}" ]; then
    _of2w_lib libOF2.so              warn "INSTALL.md §6 (needed by all coupled cases)"
    _of2w_lib librigidBodyMooring.so warn "INSTALL.md §4 (needed by all coupled cases)"
    _of2w_lib libstabRASModels.so    warn "INSTALL.md §4b (needed by turbulent cases)"
    if [ -f "${FOAM_USER_LIBBIN:-/nonexistent}/libOF2.so" ]; then
        if ldd "$FOAM_USER_LIBBIN/libOF2.so" 2>/dev/null | grep -q 'libopenfastlib.*not found'; then
            _of2w_fail "libOF2.so cannot resolve libopenfastlib.so — check OPENFAST_BUILD_DIR"
        else
            _of2w_ok "libOF2.so resolves libopenfastlib.so"
        fi
    fi
    command -v waveFoam        >/dev/null 2>&1 && _of2w_ok "waveFoam" \
        || _of2w_warn "waveFoam not on PATH (wave cases 02-04; build waves2Foam)"
    command -v overWaveDyMFoam >/dev/null 2>&1 && _of2w_ok "overWaveDyMFoam" \
        || _of2w_warn "overWaveDyMFoam not on PATH (case 06; build waves2Foam)"
fi
[ -x "$OPENFAST_BUILD_DIR/glue-codes/openfast/openfast" ] \
    && _of2w_ok "openfast driver" \
    || _of2w_warn "openfast driver not built (baseline regeneration only)"
python3 -c 'import numpy, scipy' >/dev/null 2>&1 \
    && _of2w_ok "python3 + numpy/scipy (check.py)" \
    || _of2w_warn "python3 numpy/scipy missing — case check.py scripts will fail"

# ---- summary --------------------------------------------------------------
if [ "$_OF2W_FAIL" -gt 0 ]; then
    printf 'OF2W env: %d FAIL, %d warn — fix [FAIL] lines before running cases.\n' \
        "$_OF2W_FAIL" "$_OF2W_WARN"
elif [ "$_OF2W_WARN" -gt 0 ]; then
    printf 'OF2W env ready (%d warnings — fine if you are not using those parts).\n' "$_OF2W_WARN"
else
    printf 'OF2W env ready — all checks passed.\n'
fi
unset -f _of2w_ok _of2w_warn _of2w_fail _of2w_lib _of2w_ldadd
[ "$_OF2W_FAIL" -eq 0 ]
