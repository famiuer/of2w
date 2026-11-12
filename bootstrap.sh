#!/usr/bin/env bash
# bootstrap.sh — build the complete OF2W stack with one command.
#
#   ./bootstrap.sh                 # everything, in order
#   ./bootstrap.sh --list          # show steps and their status
#   ./bootstrap.sh --force STEP    # rebuild one step even if done
#   ./bootstrap.sh --skip STEP     # skip a step this run (repeatable)
#
# Steps (in order):  openfast moordyn foammooring stabras coupling of2 waves2foam
# Each step logs to build/logs/<step>.log and leaves build/logs/<step>.done;
# finished steps are skipped on re-run, so re-running after a failure
# continues where it stopped. Prerequisites: see INSTALL.md (OpenFOAM v2306,
# gcc/gfortran, CMake>=3.16, MPI, BLAS/LAPACK).
set -uo pipefail
cd "$(dirname "$0")"
ROOT=$PWD
LOGS=$ROOT/build/logs
mkdir -p "$LOGS"
STEPS=(openfast moordyn discon foammooring stabras coupling of2 waves2foam)
SKIP=()

while [ $# -gt 0 ]; do case "$1" in
    --list)  for s in "${STEPS[@]}"; do
                 [ -f "$LOGS/$s.done" ] && printf '  done     %s\n' "$s" \
                                        || printf '  pending  %s\n' "$s"
             done; exit 0 ;;
    --force) rm -f "$LOGS/$2.done"; shift 2 ;;
    --skip)  SKIP+=("$2"); shift 2 ;;
    *) echo "usage: $0 [--list] [--force STEP] [--skip STEP]"; exit 1 ;;
esac; done

need_openfoam() {
    [ -n "${WM_PROJECT_DIR:-}" ] && command -v wmake >/dev/null && return 0
    # try the same auto-detection env.sh uses
    for c in "${OF2W_OPENFOAM_BASHRC:-}" \
             /usr/lib/openfoam/openfoam2306/etc/bashrc \
             "$HOME/OpenFOAM/OpenFOAM-v2306/etc/bashrc"; do
        [ -n "$c" ] && [ -f "$c" ] && { set +u; . "$c"; set -u; break; }
    done
    [ -n "${WM_PROJECT_DIR:-}" ] && command -v wmake >/dev/null || {
        echo "ERROR: OpenFOAM env not found. 'module load openfoam' + export"
        echo "       OF2W_OPENFOAM_BASHRC=/path/to/etc/bashrc, then re-run."
        exit 1; }
}

run_step() {  # $1 name, rest: command (run in a subshell, logged)
    local s=$1; shift
    for x in ${SKIP[@]+"${SKIP[@]}"}; do [ "$x" = "$s" ] && { echo "-- $s: skipped (--skip)"; return 0; }; done
    [ -f "$LOGS/$s.done" ] && { echo "-- $s: done, skip"; return 0; }
    echo "== $s: building (log: build/logs/$s.log)"
    if ( "$@" ) > "$LOGS/$s.log" 2>&1; then
        touch "$LOGS/$s.done"; echo "   $s OK"
    else
        echo "   $s FAILED — see build/logs/$s.log (last lines:)"
        tail -5 "$LOGS/$s.log" | sed 's/^/   | /'
        echo "   Fix the cause (INSTALL.md troubleshooting), then re-run ./bootstrap.sh"
        exit 1
    fi
}

step_openfast() {
    cmake -S external/openfast -B build/openfast -DCMAKE_BUILD_TYPE=Release
    cmake --build build/openfast -j"$(nproc)"
    [ -f build/openfast/modules/openfast-library/libopenfastlib.so ]
}
step_moordyn() {
    cmake -S external/moordyn -B build/moordyn -DCMAKE_BUILD_TYPE=Release \
          -DPYTHON_WRAPPER=OFF -DFORTRAN_WRAPPER=OFF
    cmake --build build/moordyn -j"$(nproc)"
    cmake --install build/moordyn --prefix "$ROOT/build/moordyn/install"
}
step_discon() {
    # ServoDyn's Bladed-interface controller: built from the shipped NREL
    # source (binaries are not distributed; .gitignore excludes *.so)
    ./scripts/build_discon.sh
}
step_foammooring() {
    export MOORDYN_INSTALL_DIR=$ROOT/build/moordyn/install
    cd external/foamMooring && ./Allwmake
    [ -f "$FOAM_USER_LIBBIN/librigidBodyMooring.so" ]
}
step_stabras() {
    cd external/stabRAS && wmake libso
    [ -f "$FOAM_USER_LIBBIN/libstabRASModels.so" ]
}
step_coupling() {
    cmake -S coupling -B build/coupling -DCMAKE_BUILD_TYPE=Release \
          -DOPENFAST_BUILD_DIR="$ROOT/build/openfast"
    cmake --build build/coupling -j"$(nproc)"
    cd build/coupling && ctest --output-on-failure
}
step_of2() {
    export OPENFAST_BUILD_DIR=$ROOT/build/openfast
    export OPENFAST_SRC_DIR=$ROOT/external/openfast
    cd coupling/restraints/openfoam/of2 && wmake libso
    [ -f "$FOAM_USER_LIBBIN/libOF2.so" ]
}
step_waves2foam() {
    cd external/waves2Foam
    # First run: create bin/bashrc from bashrc.org with corrected paths.
    # (Never overwrite an existing bin/bashrc — it may carry user edits.)
    if [ ! -f bin/bashrc ]; then
        cp bin/bashrc.org bin/bashrc && chmod a+x bin/bashrc
        # WAVES_DIR must point at THIS submodule, not the upstream default
        sed -i "s|^export WAVES_DIR=.*|export WAVES_DIR=$ROOT/external/waves2Foam|" bin/bashrc
        # GSL: prefer pkg-config (right answer on module-based HPC)
        if command -v pkg-config >/dev/null && pkg-config --exists gsl; then
            GSL_INC=$(pkg-config --cflags-only-I gsl | sed 's/^-I//;s/ .*//')
            GSL_LIB=$(pkg-config --libs-only-L gsl | sed 's/^-L//;s/ .*//')
            [ -n "$GSL_INC" ] && sed -i "s|^export WAVES_GSL_INCLUDE=.*|export WAVES_GSL_INCLUDE=$GSL_INC|" bin/bashrc
            [ -n "$GSL_LIB" ] && sed -i "s|^export WAVES_GSL_LIB=.*|export WAVES_GSL_LIB=$GSL_LIB|" bin/bashrc
        fi
        echo "created bin/bashrc (WAVES_DIR + GSL paths); review if GSL is unusual"
    fi
    ./Allwmake   # library + ThirdParty + ALL solvers incl. waveFoam, overWaveDyMFoam
    set +u; . bin/bashrc quiet; set -u
    [ -x "$WAVES_APPBIN/waveFoam" ] && [ -x "$WAVES_APPBIN/overWaveDyMFoam" ]
}

echo "OF2W bootstrap — logs in build/logs/"
[ -d external/openfast/modules ] || {
    echo "ERROR: submodules not populated — run: git submodule update --init --recursive"; exit 1; }
run_step openfast   step_openfast
run_step moordyn    step_moordyn
run_step discon     step_discon
need_openfoam
run_step foammooring step_foammooring
run_step stabras     step_stabras
run_step coupling    step_coupling
run_step of2         step_of2
run_step waves2foam  step_waves2foam
echo
echo "All steps complete. Next:  source env.sh   then   cd cases/01_minimal_coupling && ./run.sh"
