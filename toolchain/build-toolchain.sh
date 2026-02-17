#!/usr/bin/env bash
#
# build-toolchain.sh — Build an x86_64-elf cross compiler (Binutils + GCC)
# for bare-metal kernel development.
#
# Usage:  ./toolchain/build-toolchain.sh
#
# The toolchain is installed to toolchain/local/ and provides:
#   x86_64-elf-gcc, x86_64-elf-g++, x86_64-elf-ld, x86_64-elf-ar, etc.
#
# This script is idempotent — it skips steps whose output already exists.

set -euo pipefail

# ── Versions ──────────────────────────────────────────────────────────────────
BINUTILS_VERSION="2.43.1"
GCC_VERSION="14.2.0"
TARGET="x86_64-elf"

# ── Directories ───────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${SCRIPT_DIR}/local"
SRC_DIR="${SCRIPT_DIR}/src"
BUILD_DIR="${SCRIPT_DIR}/build"

BINUTILS_TARBALL="binutils-${BINUTILS_VERSION}.tar.xz"
GCC_TARBALL="gcc-${GCC_VERSION}.tar.xz"
BINUTILS_URL="https://ftp.gnu.org/gnu/binutils/${BINUTILS_TARBALL}"
GCC_URL="https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VERSION}/${GCC_TARBALL}"

# Number of parallel jobs for make.
JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# ── Color helpers ─────────────────────────────────────────────────────────────
bold()  { printf '\033[1m%s\033[0m\n' "$*"; }
green() { printf '\033[1;32m%s\033[0m\n' "$*"; }
red()   { printf '\033[1;31m%s\033[0m\n' "$*"; }

# ── Prerequisite check ────────────────────────────────────────────────────────
check_prereqs() {
    local missing=()
    for cmd in gcc g++ make bison flex texinfo makeinfo tar wget; do
        # 'texinfo' is a package name; the actual binary is 'makeinfo'.
        [[ "$cmd" == "texinfo" ]] && continue
        if ! command -v "$cmd" &>/dev/null; then
            missing+=("$cmd")
        fi
    done

    if (( ${#missing[@]} )); then
        red "Missing required tools: ${missing[*]}"
        echo "On Debian/Ubuntu:  sudo apt install build-essential bison flex libgmp-dev libmpc-dev libmpfr-dev texinfo wget"
        echo "On Fedora:         sudo dnf install gcc gcc-c++ make bison flex gmp-devel mpfr-devel libmpc-devel texinfo wget"
        echo "On Arch:           sudo pacman -S base-devel gmp mpfr libmpc texinfo wget"
        exit 1
    fi
}

# ── Early exit if already built ───────────────────────────────────────────────
if [[ -x "${PREFIX}/bin/${TARGET}-gcc" ]]; then
    green "Cross compiler already built: ${PREFIX}/bin/${TARGET}-gcc"
    "${PREFIX}/bin/${TARGET}-gcc" --version | head -1
    exit 0
fi

bold "=== Building ${TARGET} cross compiler ==="
bold "    Binutils ${BINUTILS_VERSION}  |  GCC ${GCC_VERSION}"
bold "    Install prefix: ${PREFIX}"
bold "    Parallel jobs:  ${JOBS}"
echo

check_prereqs

export PATH="${PREFIX}/bin:${PATH}"

mkdir -p "${SRC_DIR}" "${BUILD_DIR}" "${PREFIX}"

# ── Download sources ──────────────────────────────────────────────────────────
download() {
    local url="$1" dest="$2"
    if [[ -f "${dest}" ]]; then
        bold "Already downloaded: $(basename "${dest}")"
    else
        bold "Downloading $(basename "${dest}")..."
        wget -q --show-progress -O "${dest}" "${url}"
    fi
}

download "${BINUTILS_URL}" "${SRC_DIR}/${BINUTILS_TARBALL}"
download "${GCC_URL}" "${SRC_DIR}/${GCC_TARBALL}"

# ── Extract sources ───────────────────────────────────────────────────────────
extract() {
    local tarball="$1" name="$2"
    if [[ -d "${SRC_DIR}/${name}" ]]; then
        bold "Already extracted: ${name}"
    else
        bold "Extracting ${name}..."
        tar xf "${tarball}" -C "${SRC_DIR}"
    fi
}

extract "${SRC_DIR}/${BINUTILS_TARBALL}" "binutils-${BINUTILS_VERSION}"
extract "${SRC_DIR}/${GCC_TARBALL}" "gcc-${GCC_VERSION}"

# ── Download GCC prerequisites (GMP, MPFR, MPC, ISL) ─────────────────────────
if [[ ! -d "${SRC_DIR}/gcc-${GCC_VERSION}/gmp" ]]; then
    bold "Downloading GCC prerequisites..."
    (cd "${SRC_DIR}/gcc-${GCC_VERSION}" && ./contrib/download_prerequisites)
else
    bold "GCC prerequisites already present."
fi

# ── Build Binutils ────────────────────────────────────────────────────────────
if [[ -x "${PREFIX}/bin/${TARGET}-ld" ]]; then
    bold "Binutils already installed — skipping."
else
    bold "Building Binutils ${BINUTILS_VERSION}..."
    rm -rf "${BUILD_DIR}/binutils"
    mkdir -p "${BUILD_DIR}/binutils"
    (
        cd "${BUILD_DIR}/binutils"
        "${SRC_DIR}/binutils-${BINUTILS_VERSION}/configure" \
            --target="${TARGET}" \
            --prefix="${PREFIX}" \
            --with-sysroot \
            --disable-nls \
            --disable-werror
        make -j"${JOBS}"
        make install
    )
    green "Binutils installed."
fi

# ── Build GCC ─────────────────────────────────────────────────────────────────
if [[ -x "${PREFIX}/bin/${TARGET}-gcc" ]]; then
    bold "GCC already installed — skipping."
else
    bold "Building GCC ${GCC_VERSION}..."
    rm -rf "${BUILD_DIR}/gcc"
    mkdir -p "${BUILD_DIR}/gcc"
    (
        cd "${BUILD_DIR}/gcc"
        "${SRC_DIR}/gcc-${GCC_VERSION}/configure" \
            --target="${TARGET}" \
            --prefix="${PREFIX}" \
            --disable-nls \
            --enable-languages=c,c++ \
            --without-headers \
            --disable-hosted-libstdcxx
        make -j"${JOBS}" all-gcc
        make -j"${JOBS}" all-target-libgcc
        make install-gcc
        make install-target-libgcc
    )
    green "GCC installed."
fi

# ── Done ──────────────────────────────────────────────────────────────────────
echo
green "=== Cross compiler ready ==="
"${PREFIX}/bin/${TARGET}-gcc" --version | head -1
"${PREFIX}/bin/${TARGET}-ld"  --version | head -1
