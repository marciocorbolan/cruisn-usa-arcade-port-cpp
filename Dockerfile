# Ambiente de build/execução do projeto REA (port de Cruis'n USA)

# ---------------------------------------------------------------------------
# Estágio 1: compila o unidasm (desmontador universal do MAME) a partir do
# código-fonte oficial, numa versão fixa. Desmonta o TMS320C31 (arquitetura
# "tms320c3x") e o ADSP-2105 da placa de som ("adsp21xx"). Só as ferramentas
# são compiladas (EMULATOR=0).
# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS unidasm-build

ARG MAME_TAG=mame0289
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential python3 curl ca-certificates pkg-config \
        libsdl2-dev libsdl2-ttf-dev libfontconfig-dev libpulse-dev libasound2-dev \
        libxinerama-dev libxi-dev libx11-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
RUN curl -fsSL "https://github.com/mamedev/mame/archive/refs/tags/${MAME_TAG}.tar.gz" \
        | tar -xz --strip-components=1

RUN make -j"$(nproc)" TOOLS=1 EMULATOR=0 USE_QTDEBUG=0 NOWERROR=1 OPTIMIZE=2 SYMBOLS=0 \
    && strip unidasm

# ---------------------------------------------------------------------------
# Estágio 2: ambiente de desenvolvimento
# ---------------------------------------------------------------------------
FROM ubuntu:24.04

ARG UID=1000
ARG GID=1000
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config git gdb unzip python3 doxygen \
        libsdl2-dev \
        libpulse0 pulseaudio-utils mesa-utils libgl1-mesa-dri \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=unidasm-build /src/unidasm /usr/local/bin/unidasm

# Usuário não-root com o mesmo UID/GID do hospedeiro (ubuntu:24.04 já traz
# o usuário "ubuntu" com 1000:1000; ele é reaproveitado/renomeado).
RUN if getent passwd ${UID} >/dev/null; then \
        usermod -l dev -d /home/dev -m "$(getent passwd ${UID} | cut -d: -f1)"; \
    else \
        groupadd -g ${GID} dev 2>/dev/null || true; \
        useradd -m -u ${UID} -g ${GID} -s /bin/bash dev; \
    fi

USER dev
WORKDIR /workspace
CMD ["bash"]
