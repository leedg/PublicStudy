# =============================================================================
# Dockerfile — Multi-stage build for NetworkModuleTest
# =============================================================================
# Stage 1: Build
# Stage 2: Runtime (minimal Debian)
# =============================================================================

# ── Stage 1: Builder ──────────────────────────────────────────────────────────
FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# Linux-docker profile: epoll backend, short timeouts
RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DPROFILE=linux-docker \
    -DENABLE_DATABASE_SUPPORT=OFF \
    -DBUILD_SERVER_ENGINE=ON \
    -DBUILD_TEST_SERVER=ON \
    -DBUILD_DB_SERVER=ON && \
    cmake --build build -j$(nproc)

# ── Stage 2: Runtime ──────────────────────────────────────────────────────────
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    libsqlite3-0 && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /src/build/bin /app/bin

# Ports
#   DBServer  : 18002
#   TestServer: 19010
EXPOSE 18002 19010

# Default entrypoint — override with docker-compose
ENTRYPOINT []
