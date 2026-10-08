#!/usr/bin/env bash
# Собирает переносимую папку MediaForge-<версия>-linux-x86_64 и архив .tar.gz:
#   bin/        mediaforge, ffmpeg (статический), qt.conf
#   lib/        все зависимости, кроме glibc
#   lib/compat/ библиотеки, которые обычно есть в системе; run.sh берёт их только при отсутствии
#   plugins/    плагины Qt (xcb, offscreen, форматы изображений, ввод)
#   translations/, licenses/, src/, run.sh, README.md
# Использование: packaging/make_bundle.sh [папка-сборки]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$(cd "${1:-$ROOT/build}" && pwd)"
VERSION="$(sed -n 's/^project(MediaForge VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
NAME="MediaForge-$VERSION-linux-x86_64"
DIST="$ROOT/dist"
STAGE="$DIST/$NAME"

for tool in patchelf ldd strip tar; do
    command -v "$tool" >/dev/null || { echo "Нужна утилита $tool" >&2; exit 1; }
done
[ -x "$BUILD/mediaforge" ] || { echo "Нет $BUILD/mediaforge — сначала соберите проект" >&2; exit 1; }

QT_PLUGINS="$(qtpaths6 --plugin-dir 2>/dev/null || qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null || echo /usr/lib/x86_64-linux-gnu/qt6/plugins)"
QT_TRANSLATIONS="$(qtpaths6 --query QT_INSTALL_TRANSLATIONS 2>/dev/null || qmake6 -query QT_INSTALL_TRANSLATIONS 2>/dev/null || echo /usr/share/qt6/translations)"

echo "[bundle] $STAGE"
rm -rf "$STAGE" "$DIST/$NAME.tar.gz"
mkdir -p "$STAGE"/{bin,lib/compat,plugins,translations,licenses}

install -m 755 "$BUILD/mediaforge" "$STAGE/bin/mediaforge"
install -m 755 "$ROOT/third_party/ffmpeg/bin/ffmpeg" "$STAGE/bin/ffmpeg"
strip --strip-unneeded "$STAGE/bin/mediaforge" "$STAGE/bin/ffmpeg"
cat > "$STAGE/bin/qt.conf" <<'EOF'
[Paths]
Prefix = ..
Plugins = plugins
Translations = translations
EOF

PLUGINS=(
    platforms/libqxcb.so
    platforms/libqoffscreen.so
    xcbglintegrations/libqxcb-glx-integration.so
    xcbglintegrations/libqxcb-egl-integration.so
    imageformats/libqjpeg.so
    imageformats/libqwebp.so
    imageformats/libqtiff.so
    imageformats/libqgif.so
    imageformats/libqico.so
    platforminputcontexts/libcomposeplatforminputcontextplugin.so
)
for p in "${PLUGINS[@]}"; do
    if [ -f "$QT_PLUGINS/$p" ]; then
        mkdir -p "$STAGE/plugins/$(dirname "$p")"
        install -m 644 "$QT_PLUGINS/$p" "$STAGE/plugins/$p"
    else
        echo "[bundle] предупреждение: нет плагина $p" >&2
    fi
done
for t in qtbase_ru.qm qt_ru.qm; do
    [ -f "$QT_TRANSLATIONS/$t" ] && install -m 644 "$QT_TRANSLATIONS/$t" "$STAGE/translations/"
done

# glibc есть везде и должна быть системной.
SKIP_RE='^(linux-vdso|ld-linux-x86-64|libc|libm|libdl|libpthread|librt|libresolv|libutil|libanl|libmvec|libnsl)\.so'
# Эти библиотеки должны соответствовать драйверам и настройкам системы.
COMPAT_RE='^(libGL|libEGL|libGLX|libOpenGL|libGLdispatch|libX11|libxcb\.so|libXau|libXdmcp|libfontconfig|libfreetype|libexpat|libz\.so|libdbus-1|libsystemd|libstdc\+\+|libgcc_s|libdrm|libgbm|libwayland|libbsd|libmd\.so|libcap\.so|liblzma|libzstd|liblz4|libgcrypt|libgpg-error)'

declare -A SEEN
ELFS=("$STAGE/bin/mediaforge")
while IFS= read -r -d '' f; do ELFS+=("$f"); done < <(find "$STAGE/plugins" -name '*.so' -print0)
for elf in "${ELFS[@]}"; do
    while read -r soname path; do
        [ -n "$soname" ] && [ -n "$path" ] || continue
        [[ "$soname" =~ $SKIP_RE ]] && continue
        [ -n "${SEEN[$soname]:-}" ] && continue
        SEEN[$soname]=1
        real="$(readlink -f "$path")"
        if [[ "$soname" =~ $COMPAT_RE ]]; then
            mkdir -p "$STAGE/lib/compat/$soname"
            install -m 644 "$real" "$STAGE/lib/compat/$soname/$soname"
        else
            install -m 644 "$real" "$STAGE/lib/$soname"
        fi
    done < <(ldd "$elf" | awk '/=> \// { print $1, $3 }')
done

# Minimum libstdc++ the bundle needs; run.sh falls back to the bundled copy if the system one is older.
need="$( (strings -a "$STAGE"/bin/mediaforge "$STAGE"/lib/*.so* 2>/dev/null || true) | grep -o 'GLIBCXX_3\.4\.[0-9]*' | sort -t. -k3 -n | tail -1)"
[ -d "$STAGE/lib/compat/libstdc++.so.6" ] && echo "$need" > "$STAGE/lib/compat/libstdc++.so.6/.needs"

patchelf --set-rpath '$ORIGIN/../lib' "$STAGE/bin/mediaforge"
for f in "$STAGE"/lib/*.so*; do patchelf --set-rpath '$ORIGIN' "$f"; done
for f in "$STAGE"/lib/compat/*/*.so*; do patchelf --set-rpath '$ORIGIN/../..' "$f"; done
while IFS= read -r -d '' f; do patchelf --set-rpath '$ORIGIN/../../lib' "$f"; done < <(find "$STAGE/plugins" -name '*.so' -print0)

install -m 755 "$ROOT/packaging/run.sh" "$STAGE/run.sh"
install -m 644 "$ROOT/README.md" "$STAGE/README.md"

# Licenses of everything that ships in the archive.
cp "$ROOT/third_party/src/ffmpeg-"*/COPYING.GPLv3 "$STAGE/licenses/FFmpeg-GPLv3.txt" 2>/dev/null || true
cp "$ROOT/third_party/src/ffmpeg-"*/LICENSE.md "$STAGE/licenses/FFmpeg-LICENSE.md" 2>/dev/null || true
if command -v dpkg >/dev/null; then
    declare -A PKGS
    for f in "$STAGE"/lib/*.so* "$STAGE"/lib/compat/*/*.so*; do
        so="$(basename "$f")"
        pkg="$(dpkg -S "/$so" 2>/dev/null | grep -v diversion | head -n1 | cut -d: -f1 || true)"
        [ -n "$pkg" ] && PKGS[$pkg]=1
    done
    for pkg in libx264-dev libvpx-dev libmp3lame-dev libvorbis-dev libopus-dev libogg-dev fonts-dejavu-core "${!PKGS[@]}"; do
        [ -f "/usr/share/doc/$pkg/copyright" ] && cp "/usr/share/doc/$pkg/copyright" "$STAGE/licenses/$pkg.copyright"
    done
fi
cat > "$STAGE/licenses/README.txt" <<'EOF'
MediaForge распространяется вместе со сторонними компонентами:
  - Qt 6 (LGPL v3) — библиотеки в lib/ и plugins/;
  - FFmpeg (собран с --enable-gpl, включает libx264 — поэтому GPL v3) — bin/ffmpeg и встроен в bin/mediaforge;
  - x264 (GPL v2+), libvpx (BSD), LAME (LGPL), Ogg/Vorbis (BSD), Opus (BSD) — статически внутри FFmpeg;
  - шрифты DejaVu (свободная лицензия Bitstream Vera) — встроены в программу;
  - прочие системные библиотеки из lib/ — лицензии в файлах *.copyright.
Исходный код MediaForge — в папке src/; исходники FFmpeg скачивает src/scripts/build_ffmpeg.sh.
EOF

# Sources and build script.
mkdir -p "$STAGE/src"
( cd "$ROOT" && tar --exclude=./build --exclude='./build-*' --exclude=./dist --exclude=./third_party -cf - . ) | tar -C "$STAGE/src" -xf -

echo "[bundle] архив"
tar -C "$DIST" -czf "$DIST/$NAME.tar.gz" "$NAME"
du -sh "$STAGE" "$DIST/$NAME.tar.gz"
