#!/usr/bin/env bash
# Build waves2Foam + overWaveDyMFoam on ham8 (Durham HPC).
#
# Deploy: copy this into  /nobackup/nnrq22/coupling/scripts/  next to env.sh and
# run it on the LOGIN node (per ARC guidance, polite -j4 compilation is allowed):
#     scp build_ham8.sh ham8:/nobackup/nnrq22/coupling/scripts/03_build_waves2foam.sh
#     ssh ham8 'bash /nobackup/nnrq22/coupling/scripts/03_build_waves2foam.sh'
#
# It is idempotent (re-running skips finished stages) and produces:
#     $FOAM_USER_LIBBIN/libwaves2Foam.so (+ GABC, Sampling)
#     $FOAM_USER_APPBIN/overWaveDyMFoam      <- on the slurm PATH automatically
#
# overWaveDyMFoam = overInterDyMFoam + waves2Foam relaxation zones. The
# waves2Foam source is the fork famiuer/waves2Foam @ of2-coupling-v0.1, which
# carries the overWaveDyMFoam solver + the solvers2306_PLUS symlink (upstream
# ogoe/waves2Foam does NOT build for v2306 as-is).

# env.sh activates OpenFOAM v2306 + openblas and sets OF2_ROOT / FOAM_USER_*.
# It must be sourced BEFORE `set -e` (the OF bashrc returns non-zero while
# probing optional ThirdParty libs).
SCRIPT_DIR=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
source "$SCRIPT_DIR/env.sh"
set -eo pipefail

LOGDIR=$SCRIPT_DIR
W2F=$OF2_ROOT/src/waves2Foam
FORK=https://github.com/famiuer/waves2Foam.git
BRANCH=of2-coupling-v0.1
export WM_NCOMPPROCS=${WM_NCOMPPROCS:-4}   # polite parallelism on the login node

# ── 1. Source: clone the fork (carries overWaveDyMFoam + ThirdParty tarballs) ──
echo "================================================================"
echo "  STAGE 1/3  fetch waves2Foam fork ($BRANCH)"
echo "================================================================"
if [[ ! -d $W2F/.git ]]; then
    git clone --branch "$BRANCH" "$FORK" "$W2F"
else
    echo "waves2Foam clone present at $W2F — leaving as-is."
fi

# ── 2. ham8 bin/bashrc: WAVES_DIR + GSL paths (system GSL lives in /lib64) ──
echo "================================================================"
echo "  STAGE 2/3  configure bin/bashrc for ham8"
echo "================================================================"
GSL_INC=/usr/include
GSL_LIB=$(dirname "$(ldconfig -p | awk '/libgsl\.so /{print $NF; exit}')")   # /lib64
if [[ ! -f $W2F/bin/bashrc ]] || ! grep -q "WAVES_DIR=$W2F\$" "$W2F/bin/bashrc"; then
    cp "$W2F/bin/bashrc.org" "$W2F/bin/bashrc"
    sed -i \
        -e "s|^export WAVES_DIR=.*|export WAVES_DIR=$W2F|" \
        -e "s|^export WAVES_GSL_INCLUDE=.*|export WAVES_GSL_INCLUDE=$GSL_INC|" \
        -e "s|^export WAVES_GSL_LIB=.*|export WAVES_GSL_LIB=$GSL_LIB|" \
        "$W2F/bin/bashrc"
    chmod +x "$W2F/bin/bashrc"
    echo "wrote $W2F/bin/bashrc  (WAVES_DIR=$W2F, GSL_LIB=$GSL_LIB)"
else
    echo "bin/bashrc already configured for this path."
fi

# ── 3. Build: ThirdParty (lapack/sparskit/OceanWave3D) + libs + solvers ──
echo "================================================================"
echo "  STAGE 3/3  Allwmake  (ThirdParty + waves2Foam + overWaveDyMFoam)"
echo "================================================================"
if [[ -f $FOAM_USER_LIBBIN/libwaves2Foam.so && -x $FOAM_USER_APPBIN/overWaveDyMFoam ]]; then
    echo "libwaves2Foam.so + overWaveDyMFoam already built — skipping."
else
    ( cd "$W2F" && ./Allwmake ) 2>&1 | tee "$LOGDIR/log.build_waves2foam"
    # Allwmake loops all solvers in $WAVES_SOL; make sure overWaveDyMFoam landed.
    source "$W2F/bin/bashrc" noPrint
    if [[ ! -x $FOAM_USER_APPBIN/overWaveDyMFoam ]]; then
        ( cd "$WAVES_SOL/overWaveDyMFoam" && wmake ) 2>&1 | tee -a "$LOGDIR/log.build_waves2foam"
    fi
fi

# ── Verify ──
echo
echo "================================================================"
echo "  VERIFY"
echo "================================================================"
ok=1
for f in "$FOAM_USER_LIBBIN/libwaves2Foam.so" \
         "$FOAM_USER_LIBBIN/libwaves2FoamGABC.so" \
         "$FOAM_USER_LIBBIN/libwaves2FoamSampling.so" \
         "$FOAM_USER_APPBIN/overWaveDyMFoam"; do
    if [[ -e $f ]]; then printf "  %-28s OK\n" "$(basename "$f")"
    else                 printf "  %-28s MISSING\n" "$(basename "$f")"; ok=0; fi
done
echo
if [[ $ok -eq 1 ]]; then
    echo "DONE. overWaveDyMFoam is at $FOAM_USER_APPBIN (already on the slurm PATH)."
    echo "Case setup: controlDict 'application overWaveDyMFoam;', DROP libwaveModels.so"
    echo "from libs, use a waves2Foam constant/waveProperties + verbose transportProperties."
else
    echo "INCOMPLETE — inspect $LOGDIR/log.build_waves2foam"; exit 1
fi
