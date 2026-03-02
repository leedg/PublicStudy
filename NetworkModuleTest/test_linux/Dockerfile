# =============================================================================
# NetworkModuleTest - Linux Build + Runtime Image
# =============================================================================
# English: Ubuntu 22.04 image with gcc-12, cmake, ninja, liburing, sqlite3
# 한글: gcc-12, cmake, ninja, liburing, sqlite3 포함 Ubuntu 22.04 이미지

FROM ubuntu:22.04

# English: Prevent interactive prompts during package install
# 한글: 패키지 설치 중 대화형 프롬프트 방지
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    gcc-12 \
    g++-12 \
    cmake \
    ninja-build \
    pkg-config \
    liburing-dev \
    libsqlite3-dev \
    dos2unix \
    && rm -rf /var/lib/apt/lists/*

# English: Use gcc-12/g++-12 as default compiler
# 한글: gcc-12/g++-12를 기본 컴파일러로 사용
ENV CC=gcc-12
ENV CXX=g++-12

WORKDIR /workspace

# English: Copy entire NetworkModuleTest tree into /workspace
# 한글: NetworkModuleTest 전체를 /workspace로 복사
COPY . .

# English: Fix line endings (CRLF→LF) and set executable bit on all scripts
# 한글: 줄 끝 변환 (CRLF→LF) 및 스크립트 실행 권한 설정
RUN dos2unix test_linux/scripts/*.sh && chmod +x test_linux/scripts/*.sh
