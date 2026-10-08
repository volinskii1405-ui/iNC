#!/usr/bin/env bash
# Сборка MediaForge из исходников:
#   1) FFmpeg 6.1 со статическими кодеками (scripts/build_ffmpeg.sh, один раз);
#   2) приложение и тесты (CMake);
#   3) тесты ядра и GUI в headless-режиме;
#   4) переносимый архив dist/MediaForge-*-linux-x86_64.tar.gz и его проверка.
#
# Параметры:
#   --deps        установить зависимости через apt (Ubuntu 24.04 / Debian 13)
#   --no-tests    не запускать тесты
#   --no-bundle   не собирать архив
#   --jobs N      число потоков сборки
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JOBS="$(nproc 2>/dev/null || echo 4)"
RUN_TESTS=1
MAKE_BUNDLE=1
INSTALL_DEPS=0
while [ $# -gt 0 ]; do
    case "$1" in
        --deps) INSTALL_DEPS=1 ;;
        --no-tests) RUN_TESTS=0 ;;
        --no-bundle) MAKE_BUNDLE=0 ;;
        --jobs) JOBS="$2"; shift ;;
        *) echo "Неизвестный параметр: $1" >&2; exit 2 ;;
    esac
    shift
done

APT_PACKAGES=(build-essential cmake ninja-build pkg-config nasm curl xz-utils patchelf binutils file
              qt6-base-dev qt6-base-dev-tools qt6-multimedia-dev qt6-image-formats-plugins qt6-translations-l10n
              libx264-dev libvpx-dev libmp3lame-dev libvorbis-dev libopus-dev libogg-dev zlib1g-dev)

if [ "$INSTALL_DEPS" = 1 ]; then
    SUDO=""
    [ "$(id -u)" != 0 ] && SUDO="sudo"
    $SUDO apt-get update
    $SUDO apt-get install -y "${APT_PACKAGES[@]}"
fi

missing=()
for tool in cmake g++ pkg-config nasm make; do
    command -v "$tool" >/dev/null || missing+=("$tool")
done
if ! pkg-config --exists Qt6Widgets Qt6Multimedia 2>/dev/null; then
    missing+=("Qt6 (qt6-base-dev, qt6-multimedia-dev)")
fi
if [ "${#missing[@]}" -gt 0 ]; then
    echo "Не хватает инструментов: ${missing[*]}" >&2
    echo "Запустите: ./build.sh --deps   (или установите вручную: ${APT_PACKAGES[*]})" >&2
    exit 1
fi

echo "== FFmpeg"
JOBS="$JOBS" "$ROOT/scripts/build_ffmpeg.sh"

echo "== MediaForge"
GENERATOR=()
command -v ninja >/dev/null && GENERATOR=(-G Ninja)
cmake -S "$ROOT" -B "$ROOT/build" "${GENERATOR[@]}" -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/build" --parallel "$JOBS"

if [ "$RUN_TESTS" = 1 ]; then
    echo "== Тесты (headless)"
    (cd "$ROOT/build" && QT_QPA_PLATFORM=offscreen ctest --output-on-failure)
fi

if [ "$MAKE_BUNDLE" = 1 ]; then
    echo "== Архив"
    "$ROOT/packaging/make_bundle.sh" "$ROOT/build"
    VERSION="$(sed -n 's/^project(MediaForge VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
    "$ROOT/packaging/verify_bundle.sh" "$ROOT/dist/MediaForge-$VERSION-linux-x86_64" "$ROOT/build/verify" || {
        echo "Проверка архива нашла проблемы (см. выше)" >&2
        exit 1
    }
fi
echo "Готово."
