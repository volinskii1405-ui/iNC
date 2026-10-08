#!/usr/bin/env bash
# Проверяет распакованный архив MediaForge:
#  1) ldd всех ELF-файлов с окружением run.sh — нет "not found";
#  2) вне архива используются только библиотеки glibc;
#  3) какая версия glibc нужна;
#  4) (под root) запуск в chroot, где кроме glibc и bash ничего нет: снимок окна
#     через offscreen-плагин и короткий экспорт видео через ffmpeg из архива.
# Использование: packaging/verify_bundle.sh <папка-архива> [папка-для-результатов]
set -uo pipefail

B="$(cd "$1" && pwd)"
OUT="${2:-$(mktemp -d)}"
mkdir -p "$OUT"
fail=0
LIBPATH="$B/lib"
for d in "$B"/lib/compat/*/; do LIBPATH="$LIBPATH:${d%/}"; done

echo "== 1. ldd (все библиотеки из архива + glibc)"
GLIBC_RE='^(linux-vdso|ld-linux-x86-64|libc|libm|libdl|libpthread|librt|libresolv|libutil|libanl|libmvec|libnsl)\.so'
mapfile -d '' ELFS < <(find "$B/bin" "$B/lib" "$B/plugins" -type f \( -name '*.so*' -o -perm -u+x \) -print0)
outside=()
for f in "${ELFS[@]}"; do
    file -b "$f" 2>/dev/null | grep -q ELF || head -c4 "$f" | grep -q ELF || continue
    out="$(LD_LIBRARY_PATH="$LIBPATH" ldd "$f" 2>&1)"
    if grep -q "not found" <<<"$out"; then
        echo "  НЕ НАЙДЕНО: ${f#$B/}"
        grep "not found" <<<"$out" | sed 's/^/    /'
        fail=1
    fi
    while read -r so path; do
        [[ "$so" =~ $GLIBC_RE ]] && continue
        [[ "$path" == "$B"/* ]] && continue
        outside+=("$so => $path (из ${f#$B/})")
    done < <(awk '/=> \// { print $1, $3 }' <<<"$out")
done
if [ "${#outside[@]}" -gt 0 ]; then
    echo "  Используются системные библиотеки помимо glibc:"
    printf '    %s\n' "${outside[@]}" | sort -u
    fail=1
else
    echo "  OK: ${#ELFS[@]} файлов, вне архива только glibc."
fi

echo "== 2. run.sh --check"
"$B/run.sh" --check | tail -1 || fail=1

echo "== 3. Требуемая версия glibc"
for f in "${ELFS[@]}"; do objdump -T "$f" 2>/dev/null; done | grep -o 'GLIBC_[0-9.]*' | sort -t. -k1,1 -k2,2n -k3,3n -u | tail -1

echo "== 4. Запуск в chroot только с glibc"
if [ "$(id -u)" != 0 ] || ! command -v chroot >/dev/null; then
    echo "  пропущено (нужен root и chroot)"
    exit $fail
fi
ROOTFS="$(mktemp -d)"
cleanup() { rm -rf "$ROOTFS"; }
trap cleanup EXIT
mkdir -p "$ROOTFS"/{bin,lib64,lib/x86_64-linux-gnu,tmp,dev,app,work}
chmod 1777 "$ROOTFS/tmp"
for so in ld-linux-x86-64.so.2 libc.so.6 libm.so.6 libpthread.so.0 libdl.so.2 librt.so.1 libresolv.so.2 libtinfo.so.6; do
    src="$(readlink -f "/lib/x86_64-linux-gnu/$so" 2>/dev/null)"
    [ -f "$src" ] && cp "$src" "$ROOTFS/lib/x86_64-linux-gnu/$so"
done
cp /lib64/ld-linux-x86-64.so.2 "$ROOTFS/lib64/" 2>/dev/null || cp "$(readlink -f /lib/x86_64-linux-gnu/ld-linux-x86-64.so.2)" "$ROOTFS/lib64/ld-linux-x86-64.so.2"
cp "$(command -v bash)" "$ROOTFS/bin/bash"
ln -s bash "$ROOTFS/bin/sh"
mknod -m 666 "$ROOTFS/dev/null" c 1 3 2>/dev/null || true
mknod -m 666 "$ROOTFS/dev/urandom" c 1 9 2>/dev/null || true
cp -a "$B/." "$ROOTFS/app/"
# Тестовые данные готовим заранее ffmpeg из архива (в chroot нет других программ).
"$B/bin/ffmpeg" -v error -y -f lavfi -i testsrc2=size=320x240:rate=25:duration=2 -f lavfi -i sine=duration=2 \
    -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest "$ROOTFS/work/in.mp4"
echo "  glibc+bash в chroot: $(ls "$ROOTFS/lib/x86_64-linux-gnu" | tr '\n' ' ')"
chroot "$ROOTFS" /bin/bash -c 'cd /work && QT_QPA_PLATFORM=offscreen HOME=/tmp XDG_RUNTIME_DIR=/tmp /bin/bash /app/run.sh --screenshot /work/chroot_shot.png --screenshot-delay 3000 /work/in.mp4' \
    > "$OUT/chroot_app.log" 2>&1
code=$?
if [ $code -eq 0 ] && [ -s "$ROOTFS/work/chroot_shot.png" ]; then
    cp "$ROOTFS/work/chroot_shot.png" "$OUT/chroot_shot.png"
    echo "  OK: программа запустилась в chroot, снимок окна: $OUT/chroot_shot.png"
else
    echo "  ОШИБКА: код $code, журнал:"
    sed 's/^/    /' "$OUT/chroot_app.log" | tail -20
    fail=1
fi
chroot "$ROOTFS" /app/bin/ffmpeg -v error -y -i /work/in.mp4 -vf setpts=PTS/2 -af atempo=2 -c:v libx264 -c:a aac /work/fast.mp4 \
    > "$OUT/chroot_ffmpeg.log" 2>&1 && [ -s "$ROOTFS/work/fast.mp4" ] && echo "  OK: ffmpeg из архива кодирует H.264/AAC в chroot" || { echo "  ОШИБКА ffmpeg в chroot"; fail=1; }
exit $fail
