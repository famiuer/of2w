# OF2W container recipe — Docker/Podman build, Apptainer-consumable.
#
#   docker build -t ghcr.io/famiuer/of2w:dev --build-arg OF2W_REF=main .
#   docker run --rm ghcr.io/famiuer/of2w:dev \
#       bash -lc 'source /opt/of2w/env.sh && cd /opt/of2w/cases/00_ci_smoke && ./run.sh'
#   apptainer pull of2w.sif docker://ghcr.io/famiuer/of2w:<tag>
#
# QC-1/QC-3: this file contains NO build logic of its own — the whole
# stack is built by /opt/of2w/bootstrap.sh, the same script a native
# user runs. If a build step is missing here, fix bootstrap.sh, not this
# file. See docs/container.md for usage (incl. multi-node MPI).
FROM ubuntu:22.04

ARG OF2W_REF=main
ARG DEBIAN_FRONTEND=noninteractive

# OpenFOAM v2306 from ESI's prebuilt apt repo + the OF2W toolchain
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl gnupg git \
    && curl -fsSL https://dl.openfoam.com/add-debian-repo.sh | bash \
    && apt-get update && apt-get install -y --no-install-recommends \
        openfoam2306-default \
        build-essential gfortran cmake flex bison pkg-config \
        libopenmpi-dev openmpi-bin \
        libopenblas-dev liblapack-dev libgsl-dev zlib1g-dev \
        python3 python3-numpy python3-scipy python3-matplotlib \
    && rm -rf /var/lib/apt/lists/*

# Clone at the pinned ref (tag for releases) with the five fork submodules.
# OF2W_SHA is informational AND the cache-buster: pass the commit you expect
# (--build-arg OF2W_SHA=$(git rev-parse HEAD)) so a moved branch re-clones
# instead of silently reusing a stale cached layer.
ARG OF2W_SHA=unspecified
RUN echo "cloning of2w ${OF2W_REF} (expecting ${OF2W_SHA})" \
    && git clone --recurse-submodules --depth 1 --shallow-submodules \
        --branch "${OF2W_REF}" https://github.com/famiuer/of2w.git /opt/of2w \
    && git -C /opt/of2w log --oneline -1

# Build the stack with the single source of build truth, one step per
# layer: a failure re-runs only its own step, finished steps stay cached.
WORKDIR /opt/of2w
RUN ./bootstrap.sh --only openfast
RUN ./bootstrap.sh --only moordyn
RUN ./bootstrap.sh --only discon
RUN ./bootstrap.sh --only foammooring
RUN ./bootstrap.sh --only stabras
RUN ./bootstrap.sh --only coupling
RUN ./bootstrap.sh --only of2
RUN ./bootstrap.sh --only waves2foam

# Relocate user-dir artifacts to $HOME-independent locations: Apptainer
# runs as the HOST user with the host $HOME bind-mounted, so anything
# left in /root/OpenFOAM/... would be invisible at runtime.
RUN bash -c 'source /usr/lib/openfoam/openfoam2306/etc/bashrc; \
    cp -av "$FOAM_USER_LIBBIN"/*.so* "$FOAM_LIBBIN"/ && \
    cp -v "$FOAM_USER_APPBIN"/* "$FOAM_APPBIN"/ 2>/dev/null; \
    rm -rf "$HOME/OpenFOAM"'

# Slim: drop object files and build intermediates, keep libs/bins/cases
RUN find /opt/of2w/build /opt/of2w/external -name '*.o' -delete \
    && rm -rf /opt/of2w/build/openfast/modules/*/CMakeFiles \
              /opt/of2w/build/coupling/CMakeFiles \
              /opt/of2w/external/openfast/.git \
              /opt/of2w/external/*/.git /opt/of2w/.git

# Tells env.sh it is inside the image (it then ignores ~/OpenFOAM user builds)
RUN touch /opt/of2w/.of2w-container

LABEL org.opencontainers.image.source="https://github.com/famiuer/of2w" \
      org.opencontainers.image.description="OF2W: OpenFOAM-OpenFAST-MoorDyn coupling for FOWT, full stack prebuilt" \
      org.opencontainers.image.licenses="GPL-3.0-or-later"
CMD ["bash"]
