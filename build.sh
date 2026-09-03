#!/bin/bash
# =============================================================================
# TrollCoin 3.0 Build Script  -  multi-platform (Linux / Windows / macOS)
#
# TrollCoin 3.0 is the legacy TrollCoin chain rebased onto Blackcoin More
# = Bitcoin Core 26.2. This script
# builds the daemon, CLI, tx, wallet-tool and Qt5 GUI for:
#
#   --native      build for THIS host, no Docker  (Mac -> Homebrew qt@5 .app; Windows ->
#                                                   MSYS2/MINGW64; Linux -> autotools)
#   --ubuntu24    Ubuntu 24.04 in Docker          (sidgrip/native-base:24.04, GCC 13)
#   --ubuntu26    Ubuntu 26.04 in Docker          (sidgrip/native-base:26.04, GCC 15)
#   --windows     Windows x86_64 .exe cross-build (sidgrip/mxe-base, MXE static Qt5)
#   --macos       macOS x86_64 .app build         (on a Mac: native Homebrew qt@5;
#                                                   on Linux: osxcross container, or set
#                                                   MAC_HOST=user@mac to build over SSH)
#   --appimage    Linux AppImage                   (sidgrip/appimage-base:22.04)
#   --all         ubuntu24 + ubuntu26 + windows + macos + appimage (resilient: skips
#                 a platform on failure and prints a summary at the end). macOS
#                 cross-compiles in the osxcross container when run on Linux, unless
#                 MAC_HOST is set.
#
# The Docker builds (ubuntu24/26, windows, appimage, and macOS on Linux) need
# Docker plus the sidgrip/*-base images on Docker Hub; pass --pull-docker to
# fetch them. On a Mac, --macos builds natively with Homebrew instead; see the
# macOS section below.
#
# Usage: ./build.sh [PLATFORM ...] [TARGET] [OPTIONS]
#   PLATFORM: one or more of --native --ubuntu24 --ubuntu26 --windows --macos --all
#             (default: --native)
#   TARGET:   daemon | qt | both          (default: both)
#   OPTIONS:
#     --jobs N        parallel make jobs            (default: nproc-1)
#     --pull-docker   docker pull any missing image from Docker Hub
#     --build-docker  docker build any missing image from ./docker/Dockerfile.*
#     --strip         strip the --native binaries  (cross builds are always stripped)
#     --copy DIR      also copy the --native binaries into DIR (e.g. ~/Desktop)
#     --clean         re-run configure + 'make clean' first (--native only)
#     --release       compress ./outputs/<platform>/ into ./outputs/release/
#                     (ready-to-ship archives + SHA256SUMS; can run standalone)
#     -h | --help     show this help
#
# Output: ./outputs/<platform>/  (Windows/, macOS/, Ubuntu-24/, Ubuntu-26/) holds
#         the raw binaries; the --native build collects into ./outputs/ directly.
#         ./outputs/release/ holds the compressed, ready-to-ship archives.
# =============================================================================
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# ---- coin identity --------------------------------------------------------
VERSION="3.0"
COIN_NAME_UPPER="TrollCoin"
DAEMON_NAME="trollcoind"
QT_NAME="trollcoin-qt"
CLI_NAME="trollcoin-cli"
TX_NAME="trollcoin-tx"
WALLET_NAME="trollcoin-wallet"
UTIL_NAME="trollcoin-util"
APP_NAME="TrollCoin-Qt"            # macOS .app bundle name
APPIMAGE_NAME="${COIN_NAME_UPPER}-${VERSION}-x86_64.AppImage"

# ---- docker images / toolchain (env-overridable) --------------------------
DOCKER_UBUNTU24="${DOCKER_UBUNTU24:-sidgrip/native-base:24.04}"
DOCKER_UBUNTU26="${DOCKER_UBUNTU26:-sidgrip/native-base:26.04}"
DOCKER_WINDOWS="${DOCKER_WINDOWS:-sidgrip/mxe-base:latest}"
DOCKER_APPIMAGE="${DOCKER_APPIMAGE:-sidgrip/appimage-base:22.04}"
DOCKER_MACOS="${DOCKER_MACOS:-sidgrip/osxcross-base:sdk-26.2}"
WIN_HOST="x86_64-w64-mingw32.static"
# macOS: on a Mac, --macos builds natively (Homebrew). On Linux it cross-compiles
# in the osxcross container (depends + CONFIG_SITE), so no Mac is required. Set
# MAC_HOST=user@mac to instead build natively over SSH on a Mac you can reach.
MAC_HOST="${MAC_HOST:-}"
MAC_REMOTE_DIR="${MAC_REMOTE_DIR:-trollcoin-build}"
# In-container source mount. Kept as a literal inside the container scripts too;
# change both if you change it here.
MOUNT="/build/trollcoin"

OUTPUT_BASE="${OUTPUT_BASE:-$SCRIPT_DIR/outputs}"

# Native autotools flags. Hardened release like Blakecoin 25.2: SQLite descriptor wallets
# (--with-sqlite=yes hard-fails configure if sqlite is missing, so a release can't silently ship
# BDB-only / Taproot-incapable) + legacy Berkeley DB + explicit --enable-hardening (RELRO/BIND_NOW/
# separate-code/PIE/FORTIFY_SOURCE=3/stack-protector — Core's default, made explicit & guaranteed).
NATIVE_CONFIGFLAGS="--with-incompatible-bdb --with-sqlite=yes --enable-hardening --disable-tests --disable-bench --disable-fuzz-binary"

# ---- logging --------------------------------------------------------------
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
info() { echo -e "${CYAN}[INFO]${NC} $*"; }
ok()   { echo -e "${GREEN}[OK]${NC} $*"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
err()  { echo -e "${RED}[ERROR]${NC} $*" >&2; }

usage() { sed -n '3,/^# ===/p' "$0" | sed -e 's/^# \{0,1\}//' -e '/^===/d'; exit "${1:-0}"; }

# ---- arg parsing ----------------------------------------------------------
PLATFORMS=()
TARGET="both"
JOBS="$(( $(nproc 2>/dev/null || echo 4) > 1 ? $(nproc 2>/dev/null || echo 4) - 1 : 1 ))"
DOCKER_MODE="local"        # local | pull | build
DO_STRIP=0
DO_CLEAN=0
DO_RELEASE=0
COPY_DIR=""

while [ $# -gt 0 ]; do
    case "$1" in
        --native)        PLATFORMS+=("native") ;;
        --ubuntu24)      PLATFORMS+=("ubuntu24") ;;
        --ubuntu26)      PLATFORMS+=("ubuntu26") ;;
        --windows|--win) PLATFORMS+=("windows") ;;
        --macos|--mac)   PLATFORMS+=("macos") ;;
        --appimage)      PLATFORMS+=("appimage") ;;
        --all)           PLATFORMS+=("ubuntu24" "ubuntu26" "windows" "macos" "appimage") ;;
        daemon|qt|both)  TARGET="$1" ;;
        --daemon)        TARGET="daemon" ;;
        --qt)            TARGET="qt" ;;
        --both)          TARGET="both" ;;
        --jobs)          JOBS="$2"; shift ;;
        --jobs=*)        JOBS="${1#*=}" ;;
        --pull-docker)   DOCKER_MODE="pull" ;;
        --build-docker)  DOCKER_MODE="build" ;;
        --strip)         DO_STRIP=1 ;;
        --clean)         DO_CLEAN=1 ;;
        --release)       DO_RELEASE=1 ;;
        --copy)          COPY_DIR="$2"; shift ;;
        --copy=*)        COPY_DIR="${1#*=}" ;;
        -h|--help)       usage 0 ;;
        *)               err "unknown argument: $1"; usage 1 ;;
    esac
    shift
done
# default to a native build, unless the only action requested is --release packaging
if [ ${#PLATFORMS[@]} -eq 0 ] && [ "$DO_RELEASE" -eq 0 ]; then PLATFORMS=("native"); fi

# ---- shared helpers -------------------------------------------------------
need() { command -v "$1" >/dev/null 2>&1 || { err "required tool not found: $1"; exit 1; }; }

# Resolve the configure --with-gui value for a target.
gui_for_target() { case "$1" in daemon) echo "--with-gui=no" ;; *) echo "--with-gui=qt5" ;; esac; }

# rsync the working tree into a temp dir for a container volume-mount.
copy_source_tree_to_tempdir() {
    rsync -a \
        --exclude '.git' \
        --exclude 'outputs' \
        --exclude '.build-cache' \
        --exclude 'staging' \
        "$SCRIPT_DIR"/ "$1"/
}

# Purge stale autotools/libtool/Qt outputs so the container regenerates them
# with its own toolchain (host moc/protoc/objects must not leak into a cross build).
clean_stale_build_artifacts() {
    local dir="$1"
    find "$dir" -type d \( -name '.deps' -o -name '.libs' -o -name 'autom4te.cache' \) \
        -prune -exec rm -rf {} + 2>/dev/null || true
    find "$dir" -type f \( \
        -name 'config.status' -o -name 'config.log' -o -name 'config.cache' -o \
        -name 'libtool' -o -name '*.o' -o -name '*.lo' -o -name '*.la' -o \
        -name '*.obj' -o -name '*.a' -o -name '*.exe' -o -name '*.Tpo' -o \
        -name '*.Plo' -o -name '*.Po' -o -name '*.dirstamp' \
    \) -delete 2>/dev/null || true
    find "$dir/src/qt" -maxdepth 1 -type f \( \
        -name '*.moc' -o -name 'moc_*.cpp' -o \
        -name 'paymentrequest.pb.cc' -o -name 'paymentrequest.pb.h' \
    \) -delete 2>/dev/null || true
    find "$dir/src/qt/forms" -maxdepth 1 -type f -name 'ui_*.h' -delete 2>/dev/null || true
}

fix_permissions() {
    local dir="$1"
    find "$dir" -name '*.sh' -exec chmod +x {} + 2>/dev/null || true
    find "$dir" \( -name 'config.guess' -o -name 'config.sub' -o -name 'install-sh' \
        -o -name 'missing' -o -name 'compile' -o -name 'depcomp' -o -name 'autogen.sh' \) \
        -exec chmod +x {} + 2>/dev/null || true
}

# Map an image name to its Dockerfile in ./docker for --build-docker.
dockerfile_for_image() {
    case "$1" in
        *native-base:24.04*) echo "Dockerfile.native-base-24.04" ;;
        *native-base:26.04*) echo "Dockerfile.native-base-26.04" ;;
        *native-base:22.04*) echo "Dockerfile.native-base-22.04" ;;
        *native-base:20.04*) echo "Dockerfile.native-base-20.04" ;;
        *mxe-base*)          echo "Dockerfile.mxe-base" ;;
        *osxcross-base*)     echo "Dockerfile.osxcross-base" ;;
        *appimage-base*)     echo "Dockerfile.appimage-base" ;;
        *)                   echo "" ;;
    esac
}

# Ensure a Docker image is available per DOCKER_MODE (local | pull | build).
ensure_docker_image() {
    local image="$1"
    if docker image inspect "$image" >/dev/null 2>&1; then
        info "Docker image present: $image"
        return 0
    fi
    case "$DOCKER_MODE" in
        pull)
            info "Pulling $image from Docker Hub ..."
            docker pull "$image" || { err "docker pull failed: $image"; return 1; }
            ;;
        build)
            local df; df="$(dockerfile_for_image "$image")"
            [ -z "$df" ] && { err "no Dockerfile mapping for image: $image"; return 1; }
            [ -f "$SCRIPT_DIR/docker/$df" ] || { err "missing $SCRIPT_DIR/docker/$df"; return 1; }
            info "Building $image from docker/$df ..."
            docker build -t "$image" -f "$SCRIPT_DIR/docker/$df" "$SCRIPT_DIR/docker/" \
                || { err "docker build failed: $image"; return 1; }
            ;;
        *)
            err "Docker image not found locally: $image"
            err "Provision it, or re-run with --pull-docker (Docker Hub) or --build-docker (./docker/)."
            return 1
            ;;
    esac
}

# Remove root-owned temp tree left by a container (files are owned by the
# container user); fall back to a plain rm.
nuke_tmpdir() {
    [ -n "${1:-}" ] || return 0
    docker run --rm -v "$1:/cleanup" alpine rm -rf /cleanup 2>/dev/null || rm -rf "$1" 2>/dev/null || true
}

# Source patches applied inside every Docker container (idempotent / guarded;
# no-ops on a tree that does not need them):
#   - QPainterPath split out of <QtGui> in Qt 5.15
#   - boost::bind placeholders moved to boost::placeholders in Boost 1.73+
read -r -d '' CONTAINER_PATCH_SNIPPET <<'PATCHEOF' || true
if [ -f src/qt/trafficgraphwidget.cpp ]; then
    grep -q "#include <QPainterPath>" src/qt/trafficgraphwidget.cpp || \
        sed -i "1i #include <QPainterPath>" src/qt/trafficgraphwidget.cpp
fi
for f in $(grep -rl "boost::bind" src/ 2>/dev/null | grep "\.cpp$"); do
    grep -q "boost/bind.hpp" "$f" || sed -i "1i #include <boost/bind.hpp>" "$f"
done
PATCHEOF

# =============================================================================
# NATIVE WINDOWS (MSYS2 / MINGW64)  -  real-Windows build, not the MXE cross.
# Berkeley DB 4.8 is built from source (MSYS2 ships a newer db); the rest comes
# from MSYS2 mingw packages. Produces standalone .exe + bundled runtime DLLs.
# =============================================================================

# Build Berkeley DB 4.8.30 from source for the MinGW host; echoes the install prefix.
# Version + hash come from depends/packages/bdb.mk so every lane stays on one BDB.
build_bdb48_mingw() {
    local jobs="$1"
    local ver hash prefix sources archive work bd got
    ver="$(sed -n 's/^$(package)_version=//p' depends/packages/bdb.mk | head -1)"
    hash="$(sed -n 's/^$(package)_sha256_hash=//p' depends/packages/bdb.mk | head -1)"
    prefix="$SCRIPT_DIR/.native-bdb48/mingw64"
    sources="$SCRIPT_DIR/.native-bdb48/sources"
    archive="$sources/db-${ver}.NC.tar.gz"
    work="$SCRIPT_DIR/.native-bdb48/work"
    if [ -f "$prefix/lib/libdb_cxx-4.8.a" ] && [ -f "$prefix/include/db_cxx.h" ]; then echo "$prefix"; return 0; fi
    mkdir -p "$sources"
    if [ ! -f "$archive" ]; then
        info "downloading Berkeley DB ${ver} (NC) ..." >&2
        curl -fL "https://download.oracle.com/berkeley-db/db-${ver}.NC.tar.gz" -o "$archive.tmp" \
            || curl -fL "https://bitcoincore.org/depends-sources/db-${ver}.NC.tar.gz" -o "$archive.tmp" \
            || { err "could not download Berkeley DB ${ver}"; return 1; }
        mv "$archive.tmp" "$archive"
    fi
    if command -v sha256sum >/dev/null 2>&1 && [ -n "$hash" ]; then
        got="$(sha256sum "$archive" | awk '{print $1}')"
        [ "$got" = "$hash" ] || { err "Berkeley DB hash mismatch (got $got, want $hash)"; return 1; }
    fi
    rm -rf "$work"; mkdir -p "$work"; tar xzf "$archive" -C "$work"
    bd="$work/db-${ver}.NC/build_unix"
    ( cd "$bd"
      # BDB 4.8's atomic.h/mutex clash with modern C++ <atomic>; rename the offenders.
      sed -i 's/__atomic_compare_exchange/__atomic_compare_exchange_db/g' ../dbinc/atomic.h
      find .. \( -name '*.h' -o -name '*.c' -o -name '*.cpp' \) -print0 | xargs -0 sed -i 's/\batomic_init\b/bdb_atomic_init/g'
      cp -f "$SCRIPT_DIR/depends/config.guess" "$SCRIPT_DIR/depends/config.sub" ../dist/
      env CFLAGS="-O2" CXXFLAGS="-O2 -std=c++11" ../dist/configure \
          --prefix="$prefix" --enable-cxx --disable-shared --disable-replication \
          --disable-atomicsupport --enable-mingw --host="$(gcc -dumpmachine)" >&2
      make -j"$jobs" libdb_cxx-4.8.a libdb-4.8.a >&2
      mkdir -p "$prefix/lib" "$prefix/include"
      cp -f db.h db_cxx.h "$prefix/include/"
      cp -f libdb-4.8.a libdb_cxx-4.8.a "$prefix/lib/"
    ) || { err "Berkeley DB 4.8 build failed"; return 1; }
    echo "$prefix"
}

# Bootstrap the native Windows environment: if we are not already inside a set-up
# MSYS2 MINGW64 shell, install MSYS2 (if missing) via PowerShell, initialise it,
# and re-exec build.sh inside MINGW64. Lets `./build.sh --native` work from any
# shell (e.g. Git Bash) on a fresh Windows box, given PowerShell + internet.
ensure_windows_native_shell() {
    local target="$1" jobs="$2"
    local msys_root="/c/msys64"
    local msys_bash="$msys_root/usr/bin/bash.exe"
    local msys_env="$msys_root/usr/bin/env.exe"
    local target_flag reexec_cmd
    case "$target" in daemon) target_flag="--daemon" ;; qt) target_flag="--qt" ;; *) target_flag="--both" ;; esac

    # Already inside a set-up MINGW64 shell -> nothing to bootstrap.
    if [ "${MSYSTEM:-}" = "MINGW64" ] && command -v pacman >/dev/null 2>&1; then return 0; fi

    if ! command -v powershell.exe >/dev/null 2>&1; then
        err "PowerShell is required to bootstrap a native Windows build."; exit 1
    fi
    if [ ! -x "$msys_bash" ] || [ ! -x "$msys_env" ]; then
        info "MSYS2 not found - installing it automatically ..."
        powershell.exe -NoProfile -ExecutionPolicy Bypass -Command '
            $ErrorActionPreference = "Stop"
            $u = "https://github.com/msys2/msys2-installer/releases/download/nightly-x86_64/msys2-base-x86_64-latest.sfx.exe"
            $e = "$env:TEMP\msys2-base-x86_64-latest.sfx.exe"
            Invoke-WebRequest -UseBasicParsing -Uri $u -OutFile $e
            & $e "-y" "-oC:\"
        '
    fi
    if [ ! -x "$msys_bash" ] || [ ! -x "$msys_env" ]; then
        err "MSYS2 install did not produce $msys_bash"; exit 1
    fi
    info "initialising MSYS2 (keyring + system update) ..."
    "$msys_env" MSYSTEM=MINGW64 CHERE_INVOKING=yes MSYS2_PATH_TYPE=inherit "$msys_bash" -lc '
        set +e
        pacman-key --init >/dev/null 2>&1
        pacman-key --populate msys2 >/dev/null 2>&1
        pacman --noconfirm -Sy >/dev/null 2>&1
        pacman --noconfirm -Syuu >/dev/null 2>&1
        pacman --noconfirm -Syuu >/dev/null 2>&1
        exit 0'
    printf -v reexec_cmd 'cd %q && ./build.sh --native %s --jobs %q' "$SCRIPT_DIR" "$target_flag" "$jobs"
    info "re-entering build.sh inside MSYS2 MINGW64 ..."
    exec "$msys_env" MSYSTEM=MINGW64 CHERE_INVOKING=yes MSYS2_PATH_TYPE=inherit "$msys_bash" -lc "$reexec_cmd"
}

# Native Windows build inside MSYS2 MINGW64. Assumes the MINGW64 toolchain is on PATH.
build_windows_native() {
    local target="$1" jobs="$2"
    local out="$OUTPUT_BASE/windows-native"
    local gui; gui="$(gui_for_target "$target")"
    echo ""; echo "==== Native Windows (MSYS2 / MINGW64) | target=$target ===="

    # Install MSYS2 + re-exec into MINGW64 if we are not already there.
    ensure_windows_native_shell "$target" "$jobs"

    if ! command -v pacman >/dev/null 2>&1; then err "native Windows build must run inside an MSYS2 shell"; return 1; fi

    # First-run MSYS2 setup: a freshly extracted MSYS2 has no keyring and a stale
    # package DB, so signed `pacman -S` installs fail until the keyring is populated
    # and the DB is synced/upgraded. Do it once (guarded on the keyring), tolerating
    # a msys2-runtime self-upgrade (this shell keeps the already-loaded runtime).
    if ! pacman-key --list-keys >/dev/null 2>&1; then
        info "initialising MSYS2 (keyring + package DB) ..."
        pacman-key --init >/dev/null 2>&1 || true
        pacman-key --populate msys2 >/dev/null 2>&1 || true
        pacman --noconfirm -Syuu >/dev/null 2>&1 || true
        pacman --noconfirm -Syuu >/dev/null 2>&1 || true
    fi

    local msys_pkgs=(autoconf automake libtool make pkgconf curl git patch tar)
    local mingw_pkgs=(mingw-w64-x86_64-gcc mingw-w64-x86_64-pkgconf mingw-w64-x86_64-boost
        mingw-w64-x86_64-openssl mingw-w64-x86_64-libevent mingw-w64-x86_64-miniupnpc mingw-w64-x86_64-sqlite3)
    if [ "$target" != daemon ]; then
        mingw_pkgs+=(mingw-w64-x86_64-qt5-base mingw-w64-x86_64-qt5-tools mingw-w64-x86_64-qrencode)
    fi
    info "installing MSYS2 packages ..."
    pacman -S --needed --noconfirm "${msys_pkgs[@]}" "${mingw_pkgs[@]}" >&2 || { err "pacman install failed"; return 1; }

    export PATH="/mingw64/bin:/usr/bin:$PATH"
    if [ "$target" != daemon ]; then
        # MSYS2 suffixes the Qt5 host tools with -qt5; give configure the plain names.
        command -v qmake    >/dev/null 2>&1 || { command -v qmake-qt5    >/dev/null 2>&1 && ln -sf "$(command -v qmake-qt5)"    /mingw64/bin/qmake; }
        command -v lrelease >/dev/null 2>&1 || { command -v lrelease-qt5 >/dev/null 2>&1 && ln -sf "$(command -v lrelease-qt5)" /mingw64/bin/lrelease; }
        export MOC=/mingw64/bin/moc UIC=/mingw64/bin/uic RCC=/mingw64/bin/rcc LRELEASE=/mingw64/bin/lrelease
    fi

    rm -rf "$out"; mkdir -p "$out"
    local host; host="$(gcc -dumpmachine)"
    local bdb; bdb="$(build_bdb48_mingw "$jobs")" || return 1
    info "Berkeley DB 4.8 prefix: $bdb"

    # Qt 5.15 needs an explicit <QPainterPath> include (same fix the Docker lanes apply).
    if [ -f src/qt/trafficgraphwidget.cpp ]; then
        grep -q "#include <QPainterPath>" src/qt/trafficgraphwidget.cpp \
            || sed -i "1i #include <QPainterPath>" src/qt/trafficgraphwidget.cpp
    fi

    local cfg="$gui --host=$host --build=$host --with-incompatible-bdb --with-sqlite=yes"
    cfg="$cfg --enable-hardening --disable-tests --disable-bench --disable-fuzz-binary --disable-zmq"
    # Boost, OpenSSL, libevent, etc. live in /mingw64/{include,lib} = the MinGW gcc
    # defaults. Do NOT pass --with-boost=/mingw64: that injects -I/mingw64/include
    # ahead of the C++ system dir and breaks `#include_next <stdlib.h>` in <cstdlib>.
    [ "$target" != daemon ] && cfg="$cfg --with-qt-bindir=/mingw64/bin"

    [ -x ./configure ] || { info "autogen.sh"; ./autogen.sh; }
    info "configure ..."
    # -include cstdint: gcc 15/16 no longer pull <cstdint> in transitively, so headers
    # using int64_t (e.g. node/interface_ui.h) miscompile without it. Same fix as --native.
    ./configure $cfg \
        BDB_CFLAGS="-I$bdb/include" \
        BDB_LIBS="-L$bdb/lib -ldb_cxx-4.8 -ldb-4.8" \
        CFLAGS="-O2" CXXFLAGS="-O2 -include cstdint -DBOOST_BIND_GLOBAL_PLACEHOLDERS" >&2 \
        || { err "configure failed"; return 1; }

    # MSYS2 ships Qt5/qrencode as DLL import libs, not static archives, but Core's
    # mingw build tells the linker to use the static versions (-> "cannot find -lQt5Core,
    # have you installed the static version?"). Clear the app static-link flags so the
    # wallet links against the DLLs; those DLLs get bundled after the build.
    if [ "$target" != daemon ] && [ -f src/Makefile ]; then
        sed -i 's/^LIBTOOL_APP_LDFLAGS = .*/LIBTOOL_APP_LDFLAGS =/' src/Makefile
    fi

    info "make -j$jobs"
    make -j"$jobs" >&2 || { err "make failed"; return 1; }

    # collect the .exe (autotools may leave them in src/.libs/), strip, place in outputs
    local b src
    collect_exe() { local name="$1" dir="$2"; src="$dir/$name.exe"; [ -f "$dir/.libs/$name.exe" ] && src="$dir/.libs/$name.exe"
        [ -f "$src" ] && { strip "$src" 2>/dev/null || true; cp -f "$src" "$out/$name.exe"; ok "collected $name.exe"; } || warn "missing $name.exe"; }
    if [ "$target" != qt ]; then
        for b in "$DAEMON_NAME" "$CLI_NAME" "$TX_NAME" "$WALLET_NAME" "$UTIL_NAME"; do collect_exe "$b" src; done
    fi
    [ "$target" != daemon ] && collect_exe "$QT_NAME" src/qt

    # Bundle every non-Windows-system DLL the .exe files pull in (mingw runtime, boost, Qt, ...).
    info "bundling runtime DLLs ..."
    for b in "$out"/*.exe; do
        [ -f "$b" ] || continue
        ldd "$b" 2>/dev/null | grep '=> /' | awk '{print $3}' | while read -r dll; do
            case "$(printf '%s' "$dll" | tr 'A-Z' 'a-z')" in
                /c/windows/*) ;;
                *) cp -n "$dll" "$out/" 2>/dev/null || true ;;
            esac
        done
    done
    # Qt needs the windows platform plugin next to the app + a qt.conf pointing at it.
    if [ "$target" != daemon ]; then
        local plug="" q
        for q in /mingw64/bin/qmake /mingw64/bin/qmake-qt5; do
            [ -x "$q" ] || continue
            plug="$("$q" -query QT_INSTALL_PLUGINS 2>/dev/null | tr -d '\r')"
            [ -n "$plug" ] && [ -f "$plug/platforms/qwindows.dll" ] && break; plug=""
        done
        [ -z "$plug" ] && [ -f /mingw64/share/qt5/plugins/platforms/qwindows.dll ] && plug=/mingw64/share/qt5/plugins
        if [ -n "$plug" ] && [ -f "$plug/platforms/qwindows.dll" ]; then
            mkdir -p "$out/platforms"; cp -f "$plug/platforms/qwindows.dll" "$out/platforms/"
            printf '[Paths]\nPlugins=.\n' > "$out/qt.conf"
        else warn "qwindows.dll not found; the Qt wallet may fail to launch"; fi
    fi
    ok "native Windows build -> $out"
    ls -la "$out"/*.exe 2>/dev/null || true
}

# =============================================================================
# NATIVE (direct, no Docker)  -  builds against this host's toolchain
# =============================================================================
build_native() {
    local target="$1" jobs="$2"
    local gui; gui="$(gui_for_target "$target")"
    local out="$OUTPUT_BASE"

    # "native" means "build for THIS host". Route to the right per-OS native build.
    # On Windows (MSYS2/MINGW64) that is the MinGW build; on a Mac it is the Homebrew
    # .app build (build_macos's Darwin path); on Linux it is the autotools build below.
    if [ -n "${MSYSTEM:-}" ] || case "$(uname -s 2>/dev/null)" in MINGW*|MSYS*) true;; *) false;; esac; then
        build_windows_native "$target" "$jobs"; return $?
    fi
    if [ "$(uname -s 2>/dev/null)" = "Darwin" ]; then
        build_macos "$target" "$jobs"; return $?
    fi

    info "native (host) | target=$target jobs=$jobs"
    [ -x ./configure ] || { info "autogen.sh"; ./autogen.sh; }
    # Force-include <cstdint>: GCC 15 / strict libstdc++ (e.g. Ubuntu 26.04) no longer pull it
    # in transitively, so headers using uintNN_t fail to compile without it. Harmless elsewhere.
    export CXXFLAGS="${CXXFLAGS:--g -O2} -include cstdint"
    if [ "$DO_CLEAN" = 1 ] || [ ! -f config.status ] || [ ! -f Makefile ]; then
        info "configure $gui $NATIVE_CONFIGFLAGS"
        ./configure $gui $NATIVE_CONFIGFLAGS
    else
        info "reusing existing configure (pass --clean to re-configure / switch target)"
    fi
    [ "$DO_CLEAN" = 1 ] && { info "make clean"; make clean >/dev/null 2>&1 || true; }

    info "make -j$jobs"
    case "$target" in
        daemon) make -j"$jobs" "src/$DAEMON_NAME" "src/$CLI_NAME" "src/$TX_NAME" "src/$WALLET_NAME" ;;
        qt)     make -j"$jobs" "src/qt/$QT_NAME" "src/$CLI_NAME" "src/$TX_NAME" ;;
        both)   make -j"$jobs" ;;
    esac

    mkdir -p "$out"
    local c
    for c in "src/$DAEMON_NAME" "src/$CLI_NAME" "src/$TX_NAME" "src/$WALLET_NAME" "src/$UTIL_NAME"; do
        [ -f "$c" ] && cp -f "$c" "$out/" && ok "collected $(basename "$c")"
    done
    [ "$target" != "daemon" ] && [ -f "src/qt/$QT_NAME" ] && cp -f "src/qt/$QT_NAME" "$out/" && ok "collected $QT_NAME"

    if [ "$DO_STRIP" = 1 ]; then
        for b in "$out"/trollcoin*; do [ -f "$b" ] && strip "$b" 2>/dev/null || true; done
        ok "stripped binaries in $out"
    fi
    if [ -n "$COPY_DIR" ]; then
        mkdir -p "$COPY_DIR"; cp -f "$out"/trollcoin* "$COPY_DIR"/; ok "copied binaries to $COPY_DIR"
    fi
    ok "native build complete -> $out"
    ls -la "$out"/trollcoin* 2>/dev/null || true
}

# =============================================================================
# LINUX (Docker, reproducible)  -  sidgrip/native-base:<ubuntu>
# =============================================================================
build_linux_docker() {
    local target="$1" jobs="$2" image="$3" label="$4"
    local out="$OUTPUT_BASE/$label"
    local cn="native-trollcoin-build"
    local gui; gui="$(gui_for_target "$target")"

    echo ""; echo "==== Linux ($label) | image=$image | target=$target ===="
    ensure_docker_image "$image" || return 1
    rm -rf "$out"; mkdir -p "$out"
    docker rm -f "$cn" 2>/dev/null || true

    local tmpdir; tmpdir=$(mktemp -d)
    info "copying source tree -> $tmpdir"
    copy_source_tree_to_tempdir "$tmpdir"
    clean_stale_build_artifacts "$tmpdir"
    fix_permissions "$tmpdir"

    docker create --name "$cn" \
        -e TGT="$target" -e JOBS="$jobs" -e GUI="$gui" -e PATCHES="$CONTAINER_PATCH_SNIPPET" \
        -v "$tmpdir:/build/trollcoin:rw" "$image" \
        /bin/bash -c '
set -e
cd /build/trollcoin
echo ">>> patching sources (guarded)..."; eval "$PATCHES"
echo ">>> autogen.sh"; ./autogen.sh
echo ">>> configure $GUI"
./configure $GUI --with-incompatible-bdb --with-sqlite=yes --enable-hardening --disable-tests --disable-bench \
    --disable-fuzz-binary --disable-zmq --disable-usdt \
    CXXFLAGS="-O2 -DBOOST_BIND_GLOBAL_PLACEHOLDERS -include cstdint"
echo ">>> make -j$JOBS"; make -j"$JOBS"
echo ">>> strip"
for b in src/trollcoind src/trollcoin-cli src/trollcoin-tx src/trollcoin-wallet src/trollcoin-util src/qt/trollcoin-qt; do
    [ -f "$b" ] && strip "$b" 2>/dev/null || true
done
ls -lh src/trollcoind src/trollcoin-cli src/trollcoin-tx src/trollcoin-wallet src/trollcoin-util src/qt/trollcoin-qt 2>/dev/null || true
'
    info "starting container: $cn"
    if ! docker start -a "$cn"; then
        err "Linux ($label) build FAILED (container exited non-zero — see compile errors above)"
        docker rm -f "$cn" 2>/dev/null || true
        nuke_tmpdir "$tmpdir"
        return 1
    fi

    # Collect built binaries out of the (exited) container. Retry docker cp: a single
    # transient failure under heavy RAM-drive I/O otherwise fails the whole platform.
    local b miss=0
    collect_bin() {
        local src="$1" dst="$2" name="$3" i
        for i in 1 2 3; do
            if docker cp "$src" "$dst" 2>/dev/null; then ok "collected $name"; return 0; fi
            sleep 2
        done
        warn "missing $name (docker cp failed after retries)"; miss=1; return 1
    }
    if [[ "$target" == "daemon" || "$target" == "both" ]]; then
        for b in "$DAEMON_NAME" "$CLI_NAME" "$TX_NAME" "$WALLET_NAME" "$UTIL_NAME"; do
            collect_bin "$cn:/build/trollcoin/src/$b" "$out/$b" "$b"
        done
    fi
    if [[ "$target" == "qt" || "$target" == "both" ]]; then
        collect_bin "$cn:/build/trollcoin/src/qt/$QT_NAME" "$out/$QT_NAME" "$QT_NAME"
    fi

    docker rm -f "$cn" 2>/dev/null || true
    nuke_tmpdir "$tmpdir"
    if [ "$miss" -ne 0 ]; then err "Linux ($label): expected binaries missing — build incomplete"; return 1; fi
    stage_extras "$out" "$label" "Ubuntu ${label#Ubuntu-}.04" ubuntu
    echo "==== DONE Linux ($label) -> $out ===="; ls -lh "$out" 2>/dev/null || true
}

# =============================================================================
# WINDOWS (Docker, MXE static Qt5)  -  sidgrip/mxe-base
# =============================================================================
build_windows() {
    local target="$1" jobs="$2"
    local out="$OUTPUT_BASE/Windows"
    local cn="win-trollcoin-build"
    local cfg_extra; cfg_extra="$(gui_for_target "$target") --disable-zmq"

    echo ""; echo "==== Windows | image=$DOCKER_WINDOWS | host=$WIN_HOST | target=$target ===="
    ensure_docker_image "$DOCKER_WINDOWS" || return 1
    rm -rf "$out"; mkdir -p "$out"
    docker rm -f "$cn" 2>/dev/null || true

    local tmpdir; tmpdir=$(mktemp -d)
    info "copying source tree -> $tmpdir"
    copy_source_tree_to_tempdir "$tmpdir"
    clean_stale_build_artifacts "$tmpdir"
    fix_permissions "$tmpdir"

    docker create --name "$cn" \
        -e TGT="$target" -e JOBS="$jobs" -e WIN_HOST="$WIN_HOST" \
        -e CFG_EXTRA="$cfg_extra" -e PATCHES="$CONTAINER_PATCH_SNIPPET" \
        -v "$tmpdir:/build/trollcoin:rw" "$DOCKER_WINDOWS" \
        /bin/bash -c '
set -e
cd /build/trollcoin

export PATH=/opt/mxe/usr/bin:$PATH
HOST="$WIN_HOST"
MXE_SYSROOT=/opt/mxe/usr/${HOST}
export PATH="${MXE_SYSROOT}/qt5/bin:$PATH"
export PKG_CONFIG_LIBDIR="${MXE_SYSROOT}/qt5/lib/pkgconfig:${MXE_SYSROOT}/lib/pkgconfig"
which ${HOST}-gcc >/dev/null || { echo "ERROR: cross-compiler not found"; exit 1; }

# Restore MXE OpenSSL 3.x (Qt5 was built against it; /opt/compat ships 1.1.1).
echo ">>> restoring MXE OpenSSL 3.x for Qt5..."
rm -f /opt/compat/lib/libssl.a /opt/compat/lib/libcrypto.a
if [ -d ${MXE_SYSROOT}/include/openssl.mxe.bak ]; then
    rm -rf ${MXE_SYSROOT}/include/openssl
    cp -r ${MXE_SYSROOT}/include/openssl.mxe.bak ${MXE_SYSROOT}/include/openssl
fi
cp ${MXE_SYSROOT}/lib/mxe_bak/libssl.a ${MXE_SYSROOT}/lib/libssl.a
cp ${MXE_SYSROOT}/lib/mxe_bak/libcrypto.a ${MXE_SYSROOT}/lib/libcrypto.a

echo ">>> patching sources (guarded)..."; eval "$PATCHES"

# Qt5 include flags across module subdirs.
QT5INC="${MXE_SYSROOT}/qt5/include"
QT5_CPPFLAGS="-I${QT5INC}"
for qtmod in QtCore QtGui QtWidgets QtNetwork QtDBus; do
    [ -d "${QT5INC}/${qtmod}" ] && QT5_CPPFLAGS="${QT5_CPPFLAGS} -I${QT5INC}/${qtmod}"
done

# Merge Qt 5.14+ split *Support libs into libQt5PlatformSupport.a + pc shims.
QT5LIBDIR="${MXE_SYSROOT}/qt5/lib"
if [ ! -f "${QT5LIBDIR}/libQt5PlatformSupport.a" ]; then
    echo ">>> creating merged Qt5PlatformSupport.a..."
    _save=$(pwd); mkdir -p /tmp/qt5ps && cd /tmp/qt5ps
    for lib in AccessibilitySupport DeviceDiscoverySupport EdidSupport EventDispatcherSupport FbSupport FontDatabaseSupport PlatformCompositorSupport ThemeSupport WindowsUIAutomationSupport; do
        [ -f "${QT5LIBDIR}/libQt5${lib}.a" ] && ar x "${QT5LIBDIR}/libQt5${lib}.a"
    done
    ar crs "${QT5LIBDIR}/libQt5PlatformSupport.a" *.o 2>/dev/null || ar crs "${QT5LIBDIR}/libQt5PlatformSupport.a"
    cd "$_save" && rm -rf /tmp/qt5ps
    cat > "${QT5LIBDIR}/pkgconfig/Qt5PlatformSupport.pc" <<PCEOF
Name: Qt5PlatformSupport
Description: Merged compat lib for Qt 5.14+ split modules
Version: 5.15
Cflags:
Libs: -L${QT5LIBDIR} -lQt5PlatformSupport
PCEOF
fi
for qtlib in AccessibilitySupport DeviceDiscoverySupport EdidSupport EventDispatcherSupport FbSupport FontDatabaseSupport PlatformCompositorSupport ThemeSupport WindowsUIAutomationSupport; do
    pc="${QT5LIBDIR}/pkgconfig/Qt5${qtlib}.pc"
    if [ ! -f "$pc" ] && [ -f "${QT5LIBDIR}/libQt5${qtlib}.a" ]; then
        cat > "$pc" <<PCEOF
Name: Qt5${qtlib}
Description: MXE static Qt5 ${qtlib} shim
Version: 5.15
Cflags: -I${QT5INC}
Libs: -L${QT5LIBDIR} -lQt5${qtlib}
PCEOF
    fi
done

echo ">>> autogen.sh"; ./autogen.sh
echo ">>> patching configure to skip static-Qt-plugin link probes..."
sed -i "/as_fn_error.*Could not resolve/s/as_fn_error/true #/" configure

echo ">>> configure ($HOST)..."
./configure --host=$HOST --prefix=/usr/local \
    --with-sqlite=yes --enable-hardening \
    --disable-tests --disable-bench --disable-fuzz-binary \
    --with-qt-plugindir=${MXE_SYSROOT}/qt5/plugins \
    --with-qtdbus=no \
    --with-boost=/opt/compat --with-boost-libdir=/opt/compat/lib \
    $CFG_EXTRA \
    CXXFLAGS="-O2 -DWIN32 -DMINIUPNP_STATICLIB -DBOOST_BIND_GLOBAL_PLACEHOLDERS" \
    CFLAGS="-O2 -DWIN32" \
    CPPFLAGS="-I/opt/compat/include ${QT5_CPPFLAGS}" \
    LDFLAGS="-L/opt/compat/lib -L${MXE_SYSROOT}/lib -L${MXE_SYSROOT}/qt5/lib -static" \
    BDB_CFLAGS="-I/opt/compat/include" \
    BDB_LIBS="-L/opt/compat/lib -ldb_cxx-4.8 -ldb-4.8" \
    PROTOC=/opt/mxe/usr/x86_64-pc-linux-gnu/bin/protoc

# MXE Boost 1.81 emits duplicate category singletons under C++11; move to C++17.
find . -name Makefile -type f -exec sed -i "s/-std=c++11/-std=c++17/g" {} +

# Fork ships no Qt translations: zero them out so the build does not require .qm.
if [ -f src/Makefile ]; then
    sed -i "s/^QT_QM.*=.*/QT_QM =/" src/Makefile
    sed -i "/bitcoin_.*\.qm/d" src/Makefile
    sed -i "/locale\/.*\.qm/d" src/Makefile
fi
mkdir -p src/qt
if [ -x /opt/mxe/usr/x86_64-pc-linux-gnu/bin/protoc ] && [ -f src/qt/paymentrequest.proto ]; then
    ( cd src/qt && /opt/mxe/usr/x86_64-pc-linux-gnu/bin/protoc --cpp_out=. paymentrequest.proto )
fi
cat > src/qt/bitcoin_locale.qrc <<QRC
<!DOCTYPE RCC><RCC version="1.0">
<qresource prefix="/translations">
</qresource>
</RCC>
QRC

# Static link: --start-group resolves circular Qt5 / platform-plugin deps.
if [ -f src/Makefile ]; then
    echo ">>> patching src/Makefile LIBS (--start-group + Qt5 static chain)..."
    sed -i "s|^LIBS = \(.*\)|LIBS = -Wl,--start-group \1 -L${MXE_SYSROOT}/qt5/plugins/platforms -lqwindows -L${MXE_SYSROOT}/qt5/lib -lQt5Widgets -lQt5Gui -lQt5Network -lQt5Core -lQt5PlatformSupport -lQt5AccessibilitySupport -lQt5DeviceDiscoverySupport -lQt5EdidSupport -lQt5EventDispatcherSupport -lQt5FbSupport -lQt5FontDatabaseSupport -lQt5PlatformCompositorSupport -lQt5ThemeSupport -lQt5WindowsUIAutomationSupport -lharfbuzz -lfreetype -lharfbuzz_too -lfreetype_too -lbz2 -lpng16 -lbrotlidec -lbrotlicommon -lglib-2.0 -lintl -liconv -lpcre2-8 -lpcre2-16 -lzstd -lssl -lcrypto -ld3d11 -ldxgi -ldxguid -luxtheme -ldwmapi -ldnsapi -liphlpapi -lcrypt32 -lmpr -luserenv -lnetapi32 -lversion -lcomdlg32 -loleaut32 -limm32 -lshlwapi -latomic -lz -lws2_32 -lgdi32 -luser32 -lkernel32 -ladvapi32 -lole32 -lshell32 -luuid -lwinmm -lrpcrt4 -lssp -lwinspool -lcomctl32 -lwtsapi32 -lm -Wl,--end-group|" src/Makefile
fi

# Prebuild the small static libs serially to dodge MXE/libtool archive races.
[ -f src/univalue/Makefile ] && make -C src/univalue -j1 libunivalue.la
if [ -f src/Makefile ]; then
    make -C src -j1 \
        libbitcoinconsensus_la-arith_uint256.lo \
        libbitcoinconsensus_la-hash.lo \
        libbitcoinconsensus_la-pubkey.lo \
        libbitcoinconsensus_la-uint256.lo \
        util/libbitcoinconsensus_la-strencodings.lo \
        script/libbitcoinconsensus_la-script_error.lo \
        libbitcoinconsensus.la || true
fi

echo ">>> make -j$JOBS"
if ! make -j"$JOBS"; then
    echo ">>> parallel build failed; retrying serial..."; make -j1
fi

echo ">>> strip"
for b in src/trollcoind.exe src/qt/trollcoin-qt.exe src/trollcoin-cli.exe src/trollcoin-tx.exe src/trollcoin-wallet.exe src/trollcoin-util.exe; do
    [ -f "$b" ] && ${HOST}-strip "$b" 2>/dev/null || true
done
ls -lh src/trollcoind.exe src/qt/trollcoin-qt.exe src/trollcoin-cli.exe src/trollcoin-tx.exe src/trollcoin-wallet.exe src/trollcoin-util.exe 2>/dev/null || true
'
    info "starting container: $cn"
    docker start -a "$cn"

    local b
    if [[ "$target" == "daemon" || "$target" == "both" ]]; then
        for b in "$DAEMON_NAME" "$CLI_NAME" "$TX_NAME" "$WALLET_NAME" "$UTIL_NAME"; do
            docker cp "$cn:/build/trollcoin/src/$b.exe" "$out/$b.exe" 2>/dev/null && ok "collected $b.exe" || warn "missing $b.exe"
        done
    fi
    if [[ "$target" == "qt" || "$target" == "both" ]]; then
        docker cp "$cn:/build/trollcoin/src/qt/$QT_NAME.exe" "$out/$QT_NAME.exe" 2>/dev/null && ok "collected $QT_NAME.exe" || warn "missing $QT_NAME.exe"
    fi

    docker rm -f "$cn" 2>/dev/null || true
    nuke_tmpdir "$tmpdir"
    stage_extras "$out" "windows-x86_64" "Windows x86_64" windows
    echo "==== DONE Windows -> $out ===="; ls -lh "$out"/*.exe 2>/dev/null || true
}

# =============================================================================
# macOS  -  two paths, chosen automatically by build_macos():
#   * ON a Mac            -> native Homebrew build (qt@5 + berkeley-db@4 + boost)
#   * on Linux            -> cross-compile in the osxcross container (depends +
#                            CONFIG_SITE + autotools); no Mac required
#   * MAC_HOST=user@mac   -> optional override: build natively over SSH on that Mac
# =============================================================================

# Native macOS build steps; runs with CWD = source tree and Homebrew on PATH.
# Shared by the local-Mac build and the optional MAC_HOST cross-build (fed to
# `bash -l` on stdin in both cases). Reads $GUI, $JOBS, $TGT from the environment.
read -r -d '' MACOS_BUILD <<'REMOTE' || true
set -e
export PATH="/usr/local/bin:$PATH"
export PATH="$(brew --prefix qt@5)/bin:$PATH"
export PKG_CONFIG_PATH="$(brew --prefix qt@5)/lib/pkgconfig:$(brew --prefix openssl@3)/lib/pkgconfig:/usr/local/lib/pkgconfig"
BDB_PREFIX="$(brew --prefix berkeley-db@4)"
echo ">>> autogen"; ./autogen.sh
echo ">>> configure $GUI"
./configure $GUI --with-incompatible-bdb --with-sqlite=yes --enable-hardening --disable-tests --disable-bench --disable-fuzz-binary \
    --with-boost="$(brew --prefix boost@1.85)" \
    BDB_LIBS="-L${BDB_PREFIX}/lib -ldb_cxx-4.8" BDB_CFLAGS="-I${BDB_PREFIX}/include"
echo ">>> make -j$JOBS"; make -j"$JOBS"
for b in src/trollcoind src/trollcoin-cli src/trollcoin-tx src/trollcoin-wallet src/trollcoin-util src/qt/trollcoin-qt; do
    [ -f "$b" ] && strip -x "$b" 2>/dev/null || true
done
if [ "$TGT" != daemon ]; then
    echo ">>> make deploydir (.app bundle + zip)"
    rm -rf dist *.app TrollCoin.zip
    make deploydir
fi

# Make the command-line binaries self-contained.
#
# `make deploydir` bundles dylibs into the .app, so the GUI runs anywhere. The
# loose CLI binaries never go through it and keep the build machine's Homebrew
# install names -- /usr/local/opt/berkeley-db@4/lib/libdb_cxx-4.8.dylib and
# friends. On a user's Mac without Homebrew, or on Apple Silicon where the
# prefix is /opt/homebrew, those fail at launch with a dyld error. Copy each
# non-system dependency next to the binaries and rewrite the load commands.
echo ">>> bundling CLI dylibs (dist/cli)"
CLIDIR=dist/cli
rm -rf "$CLIDIR"; mkdir -p "$CLIDIR/lib"
for b in trollcoind trollcoin-cli trollcoin-tx trollcoin-wallet trollcoin-util; do
    [ -f "src/$b" ] && cp -a "src/$b" "$CLIDIR/$b"
done

# Homebrew keeps libraries under the cellar and links them into opt/; a
# dependency can name either, and on Apple Silicon the whole prefix moves.
is_bundlable() { case "$1" in /usr/local/*|/opt/homebrew/*) return 0 ;; *) return 1 ;; esac; }

# Pull in dependencies transitively: the dylibs reference each other, and a
# missing second-level dependency fails just as hard as a missing first-level one.
pull_deps() {
    local f="$1" dep base
    for dep in $(otool -L "$f" | tail -n +2 | awk '{print $1}'); do
        is_bundlable "$dep" || continue
        base=$(basename "$dep")
        [ -f "$CLIDIR/lib/$base" ] && continue
        cp -a "$dep" "$CLIDIR/lib/$base" || continue
        chmod u+w "$CLIDIR/lib/$base"
        install_name_tool -id "@loader_path/$base" "$CLIDIR/lib/$base" 2>/dev/null || true
        pull_deps "$CLIDIR/lib/$base"
    done
}
for b in "$CLIDIR"/*; do [ -f "$b" ] && pull_deps "$b"; done

# Rewrite the references. Binaries reach the libs one level up; the libs sit
# beside each other, so they use @loader_path.
retarget() {
    local f="$1" prefix="$2" dep base
    for dep in $(otool -L "$f" | tail -n +2 | awk '{print $1}'); do
        is_bundlable "$dep" || continue
        base=$(basename "$dep")
        install_name_tool -change "$dep" "${prefix}${base}" "$f" 2>/dev/null || true
    done
}
for b in "$CLIDIR"/*; do [ -f "$b" ] && retarget "$b" "@executable_path/lib/"; done
for l in "$CLIDIR"/lib/*; do [ -f "$l" ] && retarget "$l" "@loader_path/"; done

# Fail the build rather than ship binaries that only run on this machine. The
# same posture as --with-sqlite=yes hard-failing configure: a release must not
# be able to go out silently broken.
leaked=""
for f in "$CLIDIR"/* "$CLIDIR"/lib/*; do
    [ -f "$f" ] || continue
    for dep in $(otool -L "$f" | tail -n +2 | awk '{print $1}'); do
        is_bundlable "$dep" && leaked="$leaked\n  $(basename "$f") -> $dep"
    done
done
if [ -n "$leaked" ]; then
    echo ">>> ERROR: CLI binaries still reference build-machine paths:"
    printf "%b\n" "$leaked"
    exit 1
fi
echo ">>> CLI bundle clean: $(ls "$CLIDIR/lib" | wc -l | tr -d ' ') dylibs, no build-machine paths"

echo ">>> built:"; ls -lh src/trollcoind src/qt/trollcoin-qt dist/TrollCoin-Qt.app/Contents/MacOS/* 2>/dev/null || true
REMOTE

# Copy macOS artifacts from a built tree into outputs/macOS/.
# $1 = source tree (local dir, or user@host:dir)  $2 = target  $3 = out  $4 = cp|rsync
macos_collect() {
    local from="$1" target="$2" out="$3" mode="$4" b
    fetch() { if [[ "$mode" == cp ]]; then cp -a "$from/$1" "$out/"; else rsync -a "$from/$1" "$out/"; fi; }
    if [[ "$target" == "daemon" || "$target" == "both" ]]; then
        # dist/cli holds the same binaries with their dylibs bundled and load
        # commands rewritten, so they run on a Mac without Homebrew. Fall back
        # to the raw src/ copies only if that step did not produce them.
        for b in "$DAEMON_NAME" "$CLI_NAME" "$TX_NAME" "$WALLET_NAME" "$UTIL_NAME"; do
            if fetch "dist/cli/$b" 2>/dev/null; then ok "collected $b (relocatable)"
            elif fetch "src/$b" 2>/dev/null; then warn "collected $b UNBUNDLED - needs Homebrew on the target Mac"
            else warn "missing $b"; fi
        done
        rm -rf "$out/lib"
        fetch "dist/cli/lib" 2>/dev/null && ok "collected CLI dylibs" || warn "missing CLI dylibs"
    fi
    if [[ "$target" == "qt" || "$target" == "both" ]]; then
        fetch "src/qt/$QT_NAME" 2>/dev/null && ok "collected $QT_NAME" || warn "missing $QT_NAME"
        rm -rf "$out/$APP_NAME.app"
        fetch "dist/$APP_NAME.app" 2>/dev/null && ok "collected $APP_NAME.app" || warn "missing $APP_NAME.app"
        fetch "TrollCoin.zip" 2>/dev/null && ok "collected TrollCoin.zip" || true
    fi
}

# Cross-compile the macOS binaries + .app on Linux using the osxcross container.
# Strategy = Bitcoin Core's own depends/ tree + CONFIG_SITE (builds Qt5, Boost,
# BDB 4.8, libevent, sqlite for x86_64-apple-darwin) inside sidgrip/osxcross-base,
# then autotools configure/make/make deploydir. Nothing here needs a Mac.
build_macos_cross() {
    local target="$1" jobs="$2"
    local out="$OUTPUT_BASE/macOS"
    local container="mac-trollcoin-30-build"
    local cache_root="$SCRIPT_DIR/.build-cache"
    local tmpdir=""

    echo ""; echo "==== macOS (cross-build, osxcross container) | target=$target ===="
    info "image: $DOCKER_MACOS   strategy: depends + CONFIG_SITE"
    ensure_docker_image "$DOCKER_MACOS" || return 1
    rm -rf "$out"; mkdir -p "$out"
    docker rm -f "$container" 2>/dev/null || true

    info "copying source tree to a temp build dir ..."
    tmpdir="$(mktemp -d)"
    copy_source_tree_to_tempdir "$tmpdir"
    clean_stale_build_artifacts "$tmpdir" 2>/dev/null || true
    # clean_stale_build_artifacts strips *.o/*.a but not the extensionless product
    # binaries; drop any carried over from a Linux/native build so the collection
    # step can never pick up a stale non-Mach-O binary.
    rm -f "$tmpdir/src/$DAEMON_NAME" "$tmpdir/src/$CLI_NAME" "$tmpdir/src/$TX_NAME" \
          "$tmpdir/src/$WALLET_NAME" "$tmpdir/src/$UTIL_NAME" "$tmpdir/src/qt/$QT_NAME" 2>/dev/null || true
    fix_permissions "$tmpdir"
    mkdir -p "$cache_root/macos-depends-built" "$cache_root/macos-depends-sources"

    # The in-container script is single-quoted (all expansion is container-side);
    # per-run knobs are passed as -e env vars.
    docker create \
        --name "$container" \
        -e TROLL_TARGET="$target" \
        -e TROLL_JOBS="$jobs" \
        -v "$tmpdir:/build/trollcoin:rw" \
        -v "$cache_root/macos-depends-built:/build/trollcoin/depends/built:rw" \
        -v "$cache_root/macos-depends-sources:/build/trollcoin/depends/sources:rw" \
        "$DOCKER_MACOS" \
        /bin/bash -lc '
set -euo pipefail
cd /build/trollcoin

HOST="${OSXCROSS_HOST:-}"
[[ -z "$HOST" ]] && HOST=$(ls /opt/osxcross/target/bin/ 2>/dev/null | grep -oE "x86_64-apple-darwin[0-9.]+" | head -1 || true)
[[ -z "$HOST" ]] && { echo "ERROR: could not detect macOS host triplet"; exit 1; }

SDK_ROOT="/opt/osxcross/target/SDK"
SDK_NAME="${OSXCROSS_SDK:-}"
[[ -z "$SDK_NAME" ]] && SDK_NAME=$(find "$SDK_ROOT" -maxdepth 1 -type d -name "MacOSX*.sdk" -printf "%f\n" | sort | tail -1 || true)
[[ -z "$SDK_NAME" || ! -d "$SDK_ROOT/$SDK_NAME" ]] && { echo "ERROR: no macOS SDK under $SDK_ROOT"; exit 1; }
COMPAT="Xcode-12.2-12B45b-extracted-SDK-with-libcxx-headers"
[[ -e "$SDK_ROOT/$COMPAT" ]] || ln -s "$SDK_NAME" "$SDK_ROOT/$COMPAT"
SDK_VERSION="${SDK_NAME#MacOSX}"; SDK_VERSION="${SDK_VERSION%.sdk}"

LD64_VERSION="$(${HOST}-ld -v 2>&1 | sed -n "s/.*PROJECT:ld64-\([0-9.]*\).*/\1/p" | head -1)"
[[ -z "$LD64_VERSION" ]] && { echo "ERROR: could not detect ld64 version"; exit 1; }

DEPENDS_ARGS=(); CONFIGURE_GUI="--with-gui=qt5"
[[ "$TROLL_TARGET" == "daemon" ]] && { DEPENDS_ARGS+=(NO_QT=1); CONFIGURE_GUI="--with-gui=no"; }

python3 -c "import setuptools" >/dev/null 2>&1 || {
    echo ">>> installing python3-setuptools for depends helpers"
    apt-get update -qq
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends python3-setuptools
}
# upstream depends native_clang links libtinfo.so.5, absent on Ubuntu 22.04; add the pinned compat pkg.
if ! ldconfig -p 2>/dev/null | grep -q "libtinfo.so.5"; then
    echo ">>> installing libtinfo5 compat pkg"
    DEB="/tmp/libtinfo5.deb"
    curl -fsSL "http://archive.ubuntu.com/ubuntu/pool/universe/n/ncurses/libtinfo5_6.2-0ubuntu2.1_amd64.deb" -o "$DEB"
    echo "d0f055e72d86aab0696cc993af790a78506419c18dc284cf04e7a7e96e0a7d7a  $DEB" | sha256sum -c -
    DEBIAN_FRONTEND=noninteractive dpkg -i "$DEB" >/dev/null
fi

echo ">>> depends: HOST=$HOST SDK=$SDK_NAME ($SDK_VERSION) LD64=$LD64_VERSION TARGET=$TROLL_TARGET"
make -C depends \
    HOST="$HOST" \
    SDK_PATH="$SDK_ROOT" \
    OSX_SDK_VERSION="$SDK_VERSION" \
    OSX_MIN_VERSION=11.0 \
    LD64_VERSION="$LD64_VERSION" \
    FORCE_USE_SYSTEM_CLANG=1 \
    NO_ZMQ=1 \
    "${DEPENDS_ARGS[@]}" \
    -j"$TROLL_JOBS"

DBLIB=$(ls depends/$HOST/lib/libdb_cxx-*.a 2>/dev/null | head -1)
[[ -n "$DBLIB" ]] || { echo "ERROR: depends did not build Berkeley DB (libdb_cxx) for $HOST"; exit 1; }
echo ">>> depends Berkeley DB: $(basename "$DBLIB") (built via --with-incompatible-bdb)"

BOOST_PREFIX="$PWD/depends/$HOST"
for lib in "$BOOST_PREFIX"/lib/libboost_*.a; do
    [ -e "$lib" ] || continue
    case "$lib" in *-mt.a) continue ;; esac
    mt="${lib%.a}-mt.a"; [ -f "$mt" ] || ln -sf "$(basename "$lib")" "$mt"
done

echo ">>> autogen.sh"; ./autogen.sh
echo ">>> configure (CONFIG_SITE from depends)"
CONFIG_SITE="$PWD/depends/$HOST/share/config.site" \
CXXFLAGS="${CXXFLAGS:-} -Wno-enum-constexpr-conversion" \
OBJCXXFLAGS="${OBJCXXFLAGS:-} -Wno-enum-constexpr-conversion" \
./configure \
    --prefix=/ \
    $CONFIGURE_GUI \
    --with-incompatible-bdb \
    --with-sqlite=yes \
    --enable-hardening \
    --disable-tests --disable-bench --disable-fuzz-binary --disable-zmq \
    --with-boost="$BOOST_PREFIX" --with-boost-libdir="$BOOST_PREFIX/lib"

echo ">>> make -j$TROLL_JOBS"; make -j"$TROLL_JOBS"
for b in src/trollcoind src/trollcoin-cli src/trollcoin-tx src/trollcoin-wallet src/trollcoin-util src/qt/trollcoin-qt; do
    [ -f "$b" ] && "${HOST}-strip" -x "$b" 2>/dev/null || true
done
if [[ "$TROLL_TARGET" != daemon ]]; then
    echo ">>> make deploydir (.app bundle)"
    rm -rf dist
    make deploydir
fi
echo ">>> built:"; ls -lh src/trollcoind src/qt/trollcoin-qt dist/TrollCoin-Qt.app/Contents/MacOS/* 2>/dev/null || true
' >/dev/null

    info "starting build container: $container"
    # set -e is disabled while build_macos runs inside `if ( ... )`, so check the
    # container exit code explicitly — otherwise a failed build would fall through
    # to the collection step and silently ship stale/absent binaries.
    local rc=0
    docker start -a "$container" || rc=$?
    if [ "$rc" -ne 0 ]; then
        err "macOS cross-build failed inside $container (exit $rc) — see the log above"
        docker rm -f "$container" 2>/dev/null || true
        docker run --rm -v "$tmpdir:/cleanup" alpine rm -rf /cleanup 2>/dev/null || rm -rf "$tmpdir" 2>/dev/null || true
        return 1
    fi

    # Collect with the same plain names the native macOS build produces.
    if [[ "$target" == "daemon" || "$target" == "both" ]]; then
        for b in "$DAEMON_NAME" "$CLI_NAME" "$TX_NAME" "$WALLET_NAME" "$UTIL_NAME"; do
            docker cp "$container:/build/trollcoin/src/$b" "$out/$b" 2>/dev/null && ok "collected $b" || warn "missing $b"
        done
    fi
    if [[ "$target" == "qt" || "$target" == "both" ]]; then
        docker cp "$container:/build/trollcoin/src/qt/$QT_NAME" "$out/$QT_NAME" 2>/dev/null && ok "collected $QT_NAME" || warn "missing $QT_NAME"
        rm -rf "$out/$APP_NAME.app"
        if docker cp "$container:/build/trollcoin/dist/$APP_NAME.app" "$out/$APP_NAME.app" 2>/dev/null; then
            find "$out/$APP_NAME.app" -path "*/Contents/MacOS/*" -type f -exec chmod +x {} + 2>/dev/null || true
            ok "collected $APP_NAME.app"
        else
            warn "missing $APP_NAME.app"
        fi
    fi

    docker rm -f "$container" 2>/dev/null || true
    docker run --rm -v "$tmpdir:/cleanup" alpine rm -rf /cleanup 2>/dev/null || rm -rf "$tmpdir" 2>/dev/null || true

    # Sanity: the collected products must be real macOS Mach-O (not a stale ELF),
    # and the .app must exist for a gui target. Fail loudly otherwise.
    if [[ "$target" == "daemon" || "$target" == "both" ]]; then
        if [ ! -f "$out/$DAEMON_NAME" ] || ! file -b "$out/$DAEMON_NAME" | grep -qi 'Mach-O'; then
            err "macOS: $DAEMON_NAME missing or not Mach-O ($(file -b "$out/$DAEMON_NAME" 2>/dev/null || echo absent))"; return 1
        fi
    fi
    if [[ "$target" == "qt" || "$target" == "both" ]]; then
        if [ ! -d "$out/$APP_NAME.app" ]; then err "macOS: $APP_NAME.app was not produced"; return 1; fi
    fi
    echo "==== DONE macOS -> $out ===="; ls -lh "$out" 2>/dev/null || true
}

build_macos() {
    local target="$1" jobs="$2"
    local out="$OUTPUT_BASE/macOS"
    local gui; gui="$(gui_for_target "$target")"

    if [[ "$(uname -s)" == "Darwin" ]]; then
        echo ""; echo "==== macOS (native, local build) | target=$target ===="
        rm -rf "$out"; mkdir -p "$out"
        ( cd "$SCRIPT_DIR" && TGT="$target" JOBS="$jobs" GUI="$gui" bash -l ) <<<"$MACOS_BUILD"
        macos_collect "$SCRIPT_DIR" "$target" "$out" cp
        echo "==== DONE macOS -> $out ===="; ls -lh "$out" 2>/dev/null || true
        return 0
    fi

    # Not on a Mac: default to the osxcross container cross-build (no Mac needed).
    # MAC_HOST is an optional override to build natively over SSH on a real Mac.
    if [[ -z "${MAC_HOST:-}" ]]; then
        build_macos_cross "$target" "$jobs"
        return $?
    fi
    echo ""; echo "==== macOS (native cross-build over SSH on $MAC_HOST) | target=$target ===="
    if ! ssh -o ConnectTimeout=12 -o BatchMode=yes "$MAC_HOST" 'uname -s' >/dev/null 2>&1; then
        err "cannot SSH to MAC_HOST ($MAC_HOST). Authorize it first:  ssh-copy-id $MAC_HOST"
        return 1
    fi
    rm -rf "$out"; mkdir -p "$out"
    info "syncing source -> $MAC_HOST:~/$MAC_REMOTE_DIR"
    ssh "$MAC_HOST" "mkdir -p ~/$MAC_REMOTE_DIR"
    # Sync source only — never carry a dirty tree's build artifacts (a Linux .o/.a/binary from a
    # prior native/Docker build on the orchestrator would poison the Mac's make and get mis-collected
    # as a macOS binary). The Mac does its own autogen+configure+make on a clean tree.
    rsync -a --delete \
        --exclude '.git' --exclude 'outputs' --exclude 'docker' --exclude '.build-cache' \
        --exclude 'depends/built' --exclude 'depends/sources' --exclude 'depends/work' \
        --exclude '*.o' --exclude '*.a' --exclude '*.lo' --exclude '*.la' --exclude '*.Po' --exclude '*.Plo' \
        --exclude 'src/trollcoind' --exclude 'src/trollcoin-cli' --exclude 'src/trollcoin-tx' \
        --exclude 'src/trollcoin-wallet' --exclude 'src/trollcoin-util' --exclude 'src/qt/trollcoin-qt' \
        "$SCRIPT_DIR"/ "$MAC_HOST:$MAC_REMOTE_DIR/"
    info "building natively on $MAC_HOST (Homebrew qt@5 + legacy berkeley-db@4)"
    ssh "$MAC_HOST" "cd ~/$MAC_REMOTE_DIR && TGT='$target' JOBS='$jobs' GUI='$gui' bash -l" <<<"$MACOS_BUILD"
    macos_collect "$MAC_HOST:$MAC_REMOTE_DIR" "$target" "$out" rsync
    echo "==== DONE macOS -> $out ===="; ls -lh "$out" 2>/dev/null || true
}

# =============================================================================
# Per-platform release staging extras. Each staged outputs/<platform>/ (and thus
# its release archive) ships ready-to-run: build-info.txt everywhere, README.md on
# Linux, plus a sample .conf, a desktop launcher + icon and an install-deps.sh
# (runtime deps + launcher registration) in the native Ubuntu dirs.
# =============================================================================
COIN_LOWER="${QT_NAME%-qt}"

write_build_info() {
    local out="$1" platform="$2" osname="$3"
    cat > "$out/build-info.txt" <<INFO
Coin:       $COIN_NAME_UPPER $VERSION
Target:     $TARGET
Platform:   $platform
OS:         $osname
Date:       $(date -u '+%Y-%m-%d %H:%M:%S UTC')
Source:     https://github.com/SidGrip/TrollCoin (branch 3.0)
Script:     build.sh
Profile:    hardened-release
BDB:        Berkeley DB (--with-incompatible-bdb)
SQLite:     enabled
INFO
}

write_linux_conf() {
    local out="$1" ru rp
    ru="$(tr -dc 'a-zA-Z0-9' </dev/urandom | head -c 12)"
    rp="$(tr -dc 'a-zA-Z0-9' </dev/urandom | head -c 24)"
    cat > "$out/$COIN_LOWER.conf" <<CONF
# Sample $COIN_NAME_UPPER config. Copy to ~/.$COIN_LOWER/$COIN_LOWER.conf.
# A personal wallet needs nothing here: it finds peers and syncs on its own
# (via the dnsfeed.trollcoin.com DNS seed), and staking runs with no config.
# Uncomment a line below only to enable that feature.

# Accept trollcoin-cli / RPC commands (local CLI also works via the auth cookie).
#server=1

# Fork trollcoind into the background. Daemon only; the GUI wallet ignores it.
#daemon=1

# Serve inbound connections to other nodes (needs the P2P port forwarded).
# Outbound sync works without this.
#listen=1

# RPC credentials, used only when server=1 (e.g. remote or app RPC access).
#rpcuser=$ru
#rpcpassword=$rp
#rpcallowip=127.0.0.1
CONF
}

write_linux_desktop() {
    local out="$1"
    cat > "$out/$COIN_LOWER.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=$COIN_NAME_UPPER
Comment=$COIN_NAME_UPPER Cryptocurrency Wallet
Exec=$QT_NAME
Icon=$QT_NAME
Terminal=false
StartupNotify=true
Categories=Finance;Network;
StartupWMClass=$QT_NAME
DESK
    [ -f "$SCRIPT_DIR/src/qt/res/icons/bitcoin.png" ] && cp -f "$SCRIPT_DIR/src/qt/res/icons/bitcoin.png" "$out/$COIN_LOWER-256.png"
}

write_linux_install_deps() {
    local out="$1"
    cat > "$out/install-deps.sh" <<'DEPS'
#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo ">>> Installing @@CU@@ runtime dependencies (apt)..."
sudo apt-get update -qq
sudo apt-get install -y -qq \
    libevent-2.1-7t64 libminiupnpc17 libnatpmp1 libzmq5 libqrencode4 libsqlite3-0 \
    libqt5gui5 libqt5core5a libqt5network5 libqt5widgets5 libqt5dbus5 2>/dev/null \
  || sudo apt-get install -y -qq \
    libevent-2.1-7 libminiupnpc17 libnatpmp1 libzmq5 libqrencode4 libsqlite3-0 \
    libqt5gui5 libqt5core5a libqt5network5 libqt5widgets5 libqt5dbus5

if [ -f "$SCRIPT_DIR/@@CL@@.desktop" ] && [ -f "$SCRIPT_DIR/@@CL@@-256.png" ]; then
    apps="$HOME/.local/share/applications"; icons="$HOME/.local/share/icons/hicolor/256x256/apps"
    mkdir -p "$apps" "$icons"
    cp -f "$SCRIPT_DIR/@@CL@@-256.png" "$icons/@@QT@@.png"
    cp -f "$SCRIPT_DIR/@@CL@@.desktop" "$apps/@@CL@@-qt.desktop"
    sed -i "s|^Exec=.*|Exec=$SCRIPT_DIR/@@QT@@|" "$apps/@@CL@@-qt.desktop"
    # Absolute icon path: a name-based Icon= breaks if an icon-theme.cache ever
    # appears in the user's hicolor dir without this PNG indexed.
    sed -i "s|^Icon=.*|Icon=$icons/@@QT@@.png|" "$apps/@@CL@@-qt.desktop"
    command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$apps" 2>/dev/null || true
    echo ">>> Registered desktop launcher + icon."
fi
echo ">>> Done. Run ./@@QT@@ (Qt wallet) or ./@@CL@@d -daemon (daemon)."
DEPS
    sed -i "s/@@CU@@/$COIN_NAME_UPPER/g; s/@@CL@@/$COIN_LOWER/g; s/@@QT@@/$QT_NAME/g" "$out/install-deps.sh"
    chmod +x "$out/install-deps.sh"
}

write_linux_readme() {
    local out="$1" osname="$2"
    cat > "$out/README.md" <<README
# $COIN_NAME_UPPER v$VERSION — Linux x86_64 ($osname)

## Quick start

Native Ubuntu binaries. Install runtime dependencies + register the desktop launcher:
\`\`\`bash
chmod +x install-deps.sh
./install-deps.sh
\`\`\`

Run:
\`\`\`bash
./$QT_NAME              # Qt GUI wallet
./$DAEMON_NAME -daemon  # headless daemon
\`\`\`

A sample \`$COIN_LOWER.conf\` is included; copy it to \`~/.$COIN_LOWER/$COIN_LOWER.conf\`.
Peers are discovered via the \`dnsfeed.trollcoin.com\` DNS seed.

Source: https://github.com/SidGrip/TrollCoin (branch 3.0)
README
}

# stage_extras <out_dir> <platform> <os_name> <kind: ubuntu|windows|appimage>
stage_extras() {
    local out="$1" platform="$2" osname="$3" kind="$4"
    [ -d "$out" ] || return 0
    write_build_info "$out" "$platform" "$osname"
    case "$kind" in
        ubuntu)
            write_linux_conf "$out"
            write_linux_desktop "$out"
            write_linux_install_deps "$out"
            write_linux_readme "$out" "$osname"
            ;;
        appimage)
            cat > "$out/install-deps.sh" <<'AIDEPS'
#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo ">>> Installing AppImage runtime dependency (FUSE)..."
sudo apt-get update -qq
sudo apt-get install -y -qq libfuse2t64 2>/dev/null || sudo apt-get install -y -qq libfuse2

chmod +x "$SCRIPT_DIR"/*.AppImage
echo ">>> Done. Run $(basename "$SCRIPT_DIR"/*.AppImage)"
AIDEPS
            chmod +x "$out/install-deps.sh"
            cat > "$out/README.md" <<AREAD
# $COIN_NAME_UPPER v$VERSION — Linux AppImage (x86_64)

Portable, self-contained Qt wallet. Install the FUSE runtime and make it executable:
\`\`\`bash
chmod +x install-deps.sh
./install-deps.sh
\`\`\`

Run:
\`\`\`bash
./$APPIMAGE_NAME
\`\`\`

Manual FUSE install instead: \`sudo apt install libfuse2t64\` (Ubuntu 24.04+/26.04) or \`libfuse2\` (22.04).
The wallet registers its own launcher + icon on first run.

Source: https://github.com/SidGrip/TrollCoin (branch 3.0)
AREAD
            ;;
    esac
    ok "staged release extras ($kind) in $(basename "$out")"
}

# Compress each populated outputs/<platform>/ into a ready-to-ship archive under
# outputs/release/, then (re)generate SHA256SUMS. Each archive wraps a top-level
# TrollCoin/ folder. Windows -> .zip, everything else -> .tar.gz.
package_release() {
    need tar
    local rel="$OUTPUT_BASE/release"
    mkdir -p "$rel"
    info "packaging release archives -> $rel"

    # platform-dir : archive-suffix : format
    local specs=(
        "Windows:windows-x86_64:zip"
        "Ubuntu-24:ubuntu-24.04-x86_64:tar"
        "Ubuntu-26:ubuntu-26.04-x86_64:tar"
        "macOS:macos-x86_64:tar"
    )

    local made=0 spec dir suffix fmt src stage archive entry
    for spec in "${specs[@]}"; do
        IFS=: read -r dir suffix fmt <<<"$spec"
        src="$OUTPUT_BASE/$dir"
        [ -d "$src" ] || continue

        shopt -s nullglob
        local entries=("$src"/*)
        shopt -u nullglob
        if [ ${#entries[@]} -eq 0 ]; then warn "release: skip $dir (no binaries)"; continue; fi
        [ "$fmt" = "zip" ] && need zip

        stage="$(mktemp -d)"
        mkdir -p "$stage/TrollCoin"
        for entry in "${entries[@]}"; do
            # the macOS single-app zip is redundant inside the full release bundle
            [ "$(basename "$entry")" = "TrollCoin.zip" ] && continue
            cp -a "$entry" "$stage/TrollCoin/"
        done

        if [ "$fmt" = "zip" ]; then
            archive="$rel/TrollCoin-v${VERSION}-${suffix}.zip"
            rm -f "$archive"
            ( cd "$stage" && zip -rqy "$archive" TrollCoin )
        else
            archive="$rel/TrollCoin-v${VERSION}-${suffix}.tar.gz"
            rm -f "$archive"
            tar -czf "$archive" -C "$stage" TrollCoin
        fi
        rm -rf "$stage"
        ok "release: $(basename "$archive") ($(du -h "$archive" | awk '{print $1}'))"
        made=$((made+1))
    done

    # The AppImage is built separately into outputs/AppImage/ (not a per-platform dir handled
    # above); ship it as a tar.gz like the other platforms so the README, build-info and
    # install-deps.sh travel with it.
    local aidir="$OUTPUT_BASE/AppImage"
    shopt -s nullglob
    local ais=("$aidir"/*.AppImage)
    shopt -u nullglob
    if [ ${#ais[@]} -gt 0 ]; then
        stage="$(mktemp -d)"
        mkdir -p "$stage/TrollCoin"
        cp -a "$aidir"/. "$stage/TrollCoin/"
        archive="$rel/$(basename "${ais[0]}").tar.gz"
        rm -f "$archive" "$rel"/*.AppImage
        tar -czf "$archive" -C "$stage" TrollCoin
        rm -rf "$stage"
        ok "release: $(basename "$archive") ($(du -h "$archive" | awk '{print $1}'))"
        made=$((made+1))
    fi

    if [ "$made" -eq 0 ]; then warn "release: no platform outputs found to package"; return 0; fi

    # checksums over every archive in release/ (sha256sum -c compatible)
    ( cd "$rel"
      shopt -s nullglob
      files=(*.zip *.tar.gz *.AppImage)
      shopt -u nullglob
      [ ${#files[@]} -eq 0 ] && exit 0
      if command -v sha256sum >/dev/null 2>&1; then sha256sum "${files[@]}" > SHA256SUMS
      else shasum -a 256 "${files[@]}" > SHA256SUMS; fi )
    ok "release: SHA256SUMS"
    echo "==== DONE release -> $rel ===="; ls -lh "$rel" 2>/dev/null || true
}

# ---- dispatch -------------------------------------------------------------
# =============================================================================
# AppImage (Docker, sidgrip/appimage-base:22.04)  -  portable Linux qt wallet
# Built on Ubuntu 22.04 for broad glibc compatibility; the AppDir drops libs
# that crash on newer hosts (fontconfig/freetype/gtk3) per appimage-compat notes.
# =============================================================================
build_appimage() {
    local jobs="$1"
    local image="$DOCKER_APPIMAGE"
    local out="$OUTPUT_BASE/AppImage"
    local cn="appimage-trollcoin-build"

    echo ""; echo "==== AppImage | image=$image ===="
    ensure_docker_image "$image" || return 1
    rm -rf "$out"; mkdir -p "$out"
    docker rm -f "$cn" 2>/dev/null || true

    local tmpdir; tmpdir=$(mktemp -d)
    info "copying source tree -> $tmpdir"
    copy_source_tree_to_tempdir "$tmpdir"
    clean_stale_build_artifacts "$tmpdir"
    fix_permissions "$tmpdir"

    # container build script written to a file (env-driven) to dodge nested-heredoc escaping
    cat > "$tmpdir/.appimage-build.sh" <<'SCRIPT'
#!/bin/bash
set -e
cd "$MOUNT"
echo ">>> patching sources (guarded)..."; eval "$PATCHES"
echo ">>> autogen.sh"; ./autogen.sh
echo ">>> configure (qt5)"
./configure --with-gui=qt5 --with-incompatible-bdb --with-sqlite=yes --enable-hardening --disable-tests --disable-bench \
    --disable-fuzz-binary --disable-zmq --disable-usdt \
    CXXFLAGS="-O2 -DBOOST_BIND_GLOBAL_PLACEHOLDERS -include cstdint" LDFLAGS="-static-libstdc++"
echo ">>> make qt -j$JOBS"; make -j"$JOBS" "src/qt/$QT_NAME"

QT_BIN="src/qt/$QT_NAME"
[ -f "$QT_BIN" ] || { echo "ERROR: qt binary not built at $QT_BIN"; exit 1; }
strip "$QT_BIN"

echo ">>> creating AppDir..."
APPDIR=/build/appdir
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" "$APPDIR/usr/plugins" \
    "$APPDIR/usr/share/glib-2.0/schemas" "$APPDIR/etc" \
    "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps"
cp "$QT_BIN" "$APPDIR/usr/bin/$QT_NAME"

QT_PLUGIN_DIR=""
for p in /usr/lib/x86_64-linux-gnu/qt5/plugins /usr/lib/qt5/plugins /usr/lib64/qt5/plugins; do
    [ -d "$p" ] && QT_PLUGIN_DIR="$p" && break
done
if [ -n "$QT_PLUGIN_DIR" ]; then
    cp -r "$QT_PLUGIN_DIR/platforms" "$APPDIR/usr/plugins/" 2>/dev/null || true
    for pt in platformthemes platforminputcontexts imageformats iconengines; do
        if [ -d "$QT_PLUGIN_DIR/$pt" ]; then
            mkdir -p "$APPDIR/usr/plugins/$pt"
            cp -r "$QT_PLUGIN_DIR/$pt/." "$APPDIR/usr/plugins/$pt/" 2>/dev/null || true
        fi
    done
fi

bundle_libs() {
    ldd "$1" 2>/dev/null | awk '/=>/{print $3}' | while read -r lib; do
        { [ -z "$lib" ] || [ ! -f "$lib" ]; } && continue
        case "$(basename "$lib")" in
            libc.so*|libdl.so*|libpthread.so*|libm.so*|librt.so*|libgcc_s.so*|libstdc++.so*|ld-linux*) ;;
            libfontconfig.so*|libfreetype.so*) ;;
            *) cp -nL "$lib" "$APPDIR/usr/lib/" 2>/dev/null || true ;;
        esac
    done
}
echo ">>> bundling shared libraries..."
for bin in "$APPDIR"/usr/bin/*; do [ -f "$bin" ] && bundle_libs "$bin"; done
find "$APPDIR/usr/plugins" -name '*.so' 2>/dev/null | while read -r pl; do bundle_libs "$pl"; done

# drop GTK3/atk/epoxy + qgtk3 theme plugin (segfault with newer host themes)
rm -f "$APPDIR/usr/lib/libgtk-3.so"* "$APPDIR/usr/lib/libgdk-3.so"* \
      "$APPDIR/usr/lib/libatk-bridge-2.0.so"* "$APPDIR/usr/lib/libatspi.so"* \
      "$APPDIR/usr/lib/libepoxy.so"*
rm -f "$APPDIR/usr/plugins/platformthemes/libqgtk3.so" 2>/dev/null || true

cat > "$APPDIR/usr/bin/qt.conf" <<'QTCONF'
[Paths]
Plugins = ../plugins
QTCONF

SCHEMA_DIR="$APPDIR/usr/share/glib-2.0/schemas"
cat > "$SCHEMA_DIR/org.gnome.settings-daemon.plugins.xsettings.gschema.xml" <<'SCHEMA_EOF'
<?xml version="1.0" encoding="UTF-8"?>
<schemalist>
  <enum id="org.gnome.settings-daemon.GsdFontAntialiasingMode">
    <value nick="none" value="0"/>
    <value nick="grayscale" value="1"/>
    <value nick="rgba" value="2"/>
  </enum>
  <enum id="org.gnome.settings-daemon.GsdFontHinting">
    <value nick="none" value="0"/>
    <value nick="slight" value="1"/>
    <value nick="medium" value="2"/>
    <value nick="full" value="3"/>
  </enum>
  <enum id="org.gnome.settings-daemon.GsdFontRgbaOrder">
    <value nick="rgba" value="0"/>
    <value nick="rgb" value="1"/>
    <value nick="bgr" value="2"/>
    <value nick="vrgb" value="3"/>
    <value nick="vbgr" value="4"/>
  </enum>
  <schema gettext-domain="gnome-settings-daemon" id="org.gnome.settings-daemon.plugins.xsettings" path="/org/gnome/settings-daemon/plugins/xsettings/">
    <key name="antialiasing" enum="org.gnome.settings-daemon.GsdFontAntialiasingMode">
      <default>'grayscale'</default>
    </key>
    <key name="hinting" enum="org.gnome.settings-daemon.GsdFontHinting">
      <default>'slight'</default>
    </key>
    <key name="rgba-order" enum="org.gnome.settings-daemon.GsdFontRgbaOrder">
      <default>'rgb'</default>
    </key>
  </schema>
</schemalist>
SCHEMA_EOF
glib-compile-schemas "$SCHEMA_DIR" 2>/dev/null || echo "WARN: glib-compile-schemas failed"

cat > "$APPDIR/etc/openssl.cnf" <<'SSL_EOF'
openssl_conf = openssl_init
[openssl_init]
ssl_conf = ssl_sect
[ssl_sect]
system_default = system_default_sect
[system_default_sect]
MinProtocol = TLSv1.2
SSL_EOF

cat > "$APPDIR/$COIN_UPPER.desktop" <<DESKTOP_EOF
[Desktop Entry]
Type=Application
Name=$COIN_UPPER
Comment=$COIN_UPPER $VERSION wallet
Exec=$QT_NAME
Icon=$COIN_LOWER
Categories=Network;Finance;
Terminal=false
StartupNotify=true
StartupWMClass=$COIN_LOWER-appimage
DESKTOP_EOF
cp "$APPDIR/$COIN_UPPER.desktop" "$APPDIR/usr/share/applications/"

ICON_DIR="$APPDIR/usr/share/icons/hicolor/256x256/apps"
[ -f src/qt/res/icons/bitcoin.png ] && cp src/qt/res/icons/bitcoin.png "$ICON_DIR/$COIN_LOWER.png"
ln -sf "usr/share/icons/hicolor/256x256/apps/$COIN_LOWER.png" "$APPDIR/$COIN_LOWER.png"

cat > "$APPDIR/AppRun" <<APPRUN_EOF
#!/bin/bash
HERE="\$(dirname "\$(readlink -f "\$0")")"

# Desktop integration: register a launcher in the user's XDG dir so the desktop
# environment maps our window (X11 WM_CLASS instance "$QT_NAME") to our icon in the
# taskbar/dash instead of the generic fallback. Runs every launch; idempotent.
# Runs BEFORE the AppImage env exports below: the gio metadata write needs the
# system gio with its real module dir, not the bundle-scoped GIO_MODULE_DIR.
DESK_DIR="\${XDG_DATA_HOME:-\$HOME/.local/share}/applications"
ICON_DST="\${XDG_DATA_HOME:-\$HOME/.local/share}/icons/hicolor/256x256/apps"
if mkdir -p "\$DESK_DIR" "\$ICON_DST" 2>/dev/null; then
    cp -f "\$HERE/usr/share/icons/hicolor/256x256/apps/$COIN_LOWER.png" "\$ICON_DST/$COIN_LOWER.png" 2>/dev/null
    cat > "\$DESK_DIR/$COIN_UPPER.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=$COIN_UPPER
Comment=$COIN_UPPER $VERSION wallet
Exec=\${APPIMAGE:-\$HERE/AppRun} %u
Icon=\$ICON_DST/$COIN_LOWER.png
Categories=Network;Finance;
Terminal=false
StartupNotify=true
StartupWMClass=$COIN_LOWER-appimage
DESK
    command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "\$DESK_DIR" 2>/dev/null || true
    # No gtk-update-icon-cache here: writing an icon-theme.cache into the user's
    # hicolor dir (which has no index.theme) shadows name-based icons like the
    # native wallet's Icon=trollcoin-qt. Nothing below needs the cache.
    # Also brand the .AppImage FILE itself: Nautilus and Desktop Icons NG honor gio
    # metadata::custom-icon for regular files, replacing the generic gear icon.
    if [ -n "\${APPIMAGE:-}" ] && command -v gio >/dev/null 2>&1; then
        gio set "\$APPIMAGE" metadata::custom-icon "file://\$ICON_DST/$COIN_LOWER.png" 2>/dev/null || true
        touch "\$APPIMAGE" 2>/dev/null || true
    fi
fi

export LD_LIBRARY_PATH="\$HERE/usr/lib:\$LD_LIBRARY_PATH"
export PATH="\$HERE/usr/bin:\$PATH"
export GSETTINGS_SCHEMA_DIR="\$HERE/usr/share/glib-2.0/schemas"
export GSETTINGS_BACKEND=memory
export GIO_MODULE_DIR="\$HERE/usr/lib/gio/modules"
[ -d "\$HERE/usr/plugins" ] && export QT_PLUGIN_PATH="\$HERE/usr/plugins"
export QT_QPA_PLATFORM="\${QT_QPA_PLATFORM:-xcb}"
export QT_STYLE_OVERRIDE=Fusion
export XDG_DATA_DIRS="\$HERE/usr/share:\${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export OPENSSL_CONF="\$HERE/etc/openssl.cnf"

# Distinct WM_CLASS instance so GNOME maps the AppImage window to the launcher
# above, never to the native wallet's trollcoin-qt.desktop (and vice versa).
export RESOURCE_NAME="$COIN_LOWER-appimage"

exec "\$HERE/usr/bin/$QT_NAME" "\$@"
APPRUN_EOF
chmod +x "$APPDIR/AppRun"

echo ">>> appimagetool..."
mkdir -p /build/output
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 appimagetool --no-appstream "$APPDIR" "/build/output/$APPIMAGE_NAME"
chmod +x "/build/output/$APPIMAGE_NAME"
ls -lh /build/output/
SCRIPT

    docker create --name "$cn" \
        -e MOUNT="$MOUNT" -e JOBS="$jobs" -e PATCHES="$CONTAINER_PATCH_SNIPPET" \
        -e QT_NAME="$QT_NAME" -e COIN_LOWER="trollcoin" -e COIN_UPPER="$COIN_NAME_UPPER" \
        -e VERSION="$VERSION" -e APPIMAGE_NAME="$APPIMAGE_NAME" \
        -v "$tmpdir:$MOUNT:rw" "$image" \
        /bin/bash "$MOUNT/.appimage-build.sh" >/dev/null

    info "starting container: $cn"
    if ! docker start -a "$cn"; then
        err "AppImage build FAILED (container exited non-zero — see errors above)"
        docker rm -f "$cn" 2>/dev/null || true; nuke_tmpdir "$tmpdir"; return 1
    fi
    if docker cp "$cn:/build/output/$APPIMAGE_NAME" "$out/$APPIMAGE_NAME" 2>/dev/null; then
        chmod +x "$out/$APPIMAGE_NAME"; ok "collected $APPIMAGE_NAME"
    else
        err "AppImage not produced in container"; docker rm -f "$cn" 2>/dev/null || true; nuke_tmpdir "$tmpdir"; return 1
    fi
    docker rm -f "$cn" 2>/dev/null || true
    nuke_tmpdir "$tmpdir"
    stage_extras "$out" "linux-appimage" "Linux AppImage x86_64" appimage
    echo "==== DONE AppImage -> $out ===="; ls -lh "$out" 2>/dev/null || true
}

# ---- dispatch -------------------------------------------------------------
# rsync stages the source tree for the containerized + macOS lanes; the native
# (host / MSYS2) build works in place and does not need it.
for p in "${PLATFORMS[@]}"; do case "$p" in ubuntu24|ubuntu26|windows|appimage|macos) need rsync; break;; esac; done
# Docker is required only for the containerized Linux/Windows platforms;
# macOS builds natively on a Mac over SSH, native uses this host's toolchain.
for p in "${PLATFORMS[@]}"; do case "$p" in ubuntu24|ubuntu26|windows|appimage) need docker; break;; esac; done

info "TrollCoin $VERSION  |  platforms: ${PLATFORMS[*]}  |  target: $TARGET  |  jobs: $JOBS  |  base: Blackcoin More / Bitcoin Core 26.2"

# RESULTS is a plain "plat:status" list (no associative array) so build.sh runs on
# macOS's stock bash 3.2, which lacks `declare -A`.
RESULTS=""
for p in "${PLATFORMS[@]}"; do
    r=FAIL
    case "$p" in
        native)   if ( build_native   "$TARGET" "$JOBS" );                                  then r=ok; fi ;;
        ubuntu24) if ( build_linux_docker "$TARGET" "$JOBS" "$DOCKER_UBUNTU24" "Ubuntu-24" ); then r=ok; fi ;;
        ubuntu26) if ( build_linux_docker "$TARGET" "$JOBS" "$DOCKER_UBUNTU26" "Ubuntu-26" ); then r=ok; fi ;;
        windows)  if ( build_windows  "$TARGET" "$JOBS" );                                  then r=ok; fi ;;
        macos)    if ( build_macos    "$TARGET" "$JOBS" );                                  then r=ok; fi ;;
        appimage) if ( build_appimage "$JOBS" );                                            then r=ok; fi ;;
    esac
    RESULTS="$RESULTS $p:$r"
done

if [ "$DO_RELEASE" -eq 1 ]; then package_release; fi

echo ""
echo "============================================"
echo "  TrollCoin $VERSION build summary ($TARGET)"
echo "============================================"
rc=0
for entry in $RESULTS; do
    p="${entry%%:*}"; r="${entry##*:}"
    if [ "$r" = "ok" ]; then ok "$p"; else err "$p"; rc=1; fi
done
exit "$rc"
