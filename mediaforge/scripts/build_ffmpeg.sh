#!/usr/bin/env bash
# Собирает FFmpeg (статические libav*.a + ffmpeg/ffprobe) в third_party/ffmpeg.
# Кодеки x264, vpx, lame, vorbis, opus и zlib вшиваются статически: бинарник
# ffmpeg зависит только от glibc, а приложение линкует libav* к себе внутрь.
# Статические кодеки из дистрибутива собраны без -fPIC, поэтому общие .so
# из них собрать нельзя, а статическая сборка работает.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FF_VERSION="${FF_VERSION:-6.1.1}"
TP="$ROOT/third_party"
PREFIX="$TP/ffmpeg"
SRC="$TP/src/ffmpeg-$FF_VERSION"
JOBS="${JOBS:-$(nproc)}"

if [[ -x "$PREFIX/bin/ffmpeg" && -f "$PREFIX/lib/libavcodec.a" ]]; then
    echo "[ffmpeg] уже собран: $PREFIX"
    exit 0
fi

mkdir -p "$TP/src"
if [[ ! -d "$SRC" ]]; then
    TARBALL="$TP/src/ffmpeg-$FF_VERSION.tar.xz"
    if [[ ! -f "$TARBALL" ]]; then
        echo "[ffmpeg] скачиваю исходники $FF_VERSION"
        if ! curl -fL --retry 3 -o "$TARBALL.part" "https://ffmpeg.org/releases/ffmpeg-$FF_VERSION.tar.xz"; then
            rm -f "$TARBALL.part"
            echo "[ffmpeg] ffmpeg.org недоступен, пробую apt-get source"
            (cd "$TP/src" && apt-get source --download-only ffmpeg)
            ORIG="$(ls "$TP"/src/ffmpeg_*.orig.tar.xz | head -n1)"
            cp "$ORIG" "$TARBALL.part"
        fi
        mv "$TARBALL.part" "$TARBALL"
    fi
    tar -C "$TP/src" -xf "$TARBALL"
fi

# В этой папке лежат только .a, поэтому линковщик выберет статические версии.
STATIC_DIR="$TP/static-libs"
mkdir -p "$STATIC_DIR"
for lib in x264 vpx mp3lame vorbis vorbisenc ogg opus z; do
    found=""
    for dir in /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib /usr/local/lib; do
        if [[ -f "$dir/lib$lib.a" ]]; then found="$dir/lib$lib.a"; break; fi
    done
    if [[ -z "$found" ]]; then
        echo "[ffmpeg] не найдена статическая lib$lib.a (установите dev-пакет)" >&2
        exit 1
    fi
    ln -sf "$found" "$STATIC_DIR/lib$lib.a"
done

cd "$SRC"
./configure \
    --prefix="$PREFIX" \
    --disable-shared --enable-static --enable-pic \
    --enable-gpl \
    --disable-autodetect \
    --enable-pthreads --enable-zlib \
    --enable-libx264 --enable-libvpx --enable-libmp3lame --enable-libvorbis --enable-libopus \
    --disable-avdevice --disable-postproc --disable-network \
    --disable-ffplay \
    --disable-doc --disable-debug \
    --pkg-config-flags="--static" \
    --extra-ldflags="-L$STATIC_DIR"
make -j"$JOBS"
make install
echo "[ffmpeg] готово: $PREFIX"
