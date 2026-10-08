#!/usr/bin/env bash
# Запуск MediaForge из распакованного архива. Ничего не устанавливает.
#   ./run.sh [файлы...]      — запустить программу
#   ./run.sh --check         — проверить, что все библиотеки находятся (ldd)
# Скрипт обходится встроенными командами bash, чтобы работать на минимальных системах.

self="${BASH_SOURCE[0]}"
if command -v readlink >/dev/null 2>&1; then
    self="$(readlink -f "$self")"
fi
case "$self" in
    */*) HERE="$(cd "${self%/*}" && pwd -P)" ;;
    *) HERE="$(pwd -P)" ;;
esac

# Библиотеки из lib/compat обычно есть в системе и должны совпадать с её драйверами
# и настройками (OpenGL, X11, fontconfig, libstdc++). Берём свою копию, только если
# системной нет (или libstdc++ слишком старая).
sysdirs=(/lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu /lib64 /usr/lib64 /usr/lib /lib /usr/local/lib)
find_system_lib() {
    local d
    for d in "${sysdirs[@]}"; do
        if [ -e "$d/$1" ]; then
            printf '%s' "$d/$1"
            return 0
        fi
    done
    return 1
}

extra=""
for dir in "$HERE"/lib/compat/*/; do
    [ -d "$dir" ] || continue
    soname="${dir%/}"
    soname="${soname##*/}"
    use_bundled=0
    if syslib="$(find_system_lib "$soname")"; then
        if [ -f "$dir/.needs" ] && command -v grep >/dev/null 2>&1; then
            read -r need < "$dir/.needs"
            if [ -n "$need" ] && ! grep -qa "$need" "$syslib"; then
                use_bundled=1
            fi
        fi
    else
        use_bundled=1
    fi
    if [ "$use_bundled" = 1 ]; then
        extra="$extra:${dir%/}"
    fi
done

export LD_LIBRARY_PATH="$HERE/lib$extra${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/plugins"
export MEDIAFORGE_FFMPEG="$HERE/bin/ffmpeg"
# В архиве есть только X11-плагин; под Wayland программа работает через XWayland.
if [ -z "${QT_QPA_PLATFORM:-}" ]; then
    export QT_QPA_PLATFORM=xcb
fi

if [ "${1:-}" = "--check" ]; then
    if ! command -v ldd >/dev/null 2>&1; then
        echo "ldd не найден — проверка невозможна" >&2
        exit 2
    fi
    status=0
    echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
    for f in "$HERE"/bin/mediaforge "$HERE"/bin/ffmpeg "$HERE"/plugins/*/*.so; do
        missing="$(ldd "$f" 2>&1 | grep 'not found')"
        if [ -n "$missing" ]; then
            echo "НЕ НАЙДЕНО для ${f#$HERE/}:"
            echo "$missing"
            status=1
        fi
    done
    if [ "$status" = 0 ]; then
        echo "OK: все библиотеки найдены."
    fi
    exit "$status"
fi

if [ "$QT_QPA_PLATFORM" = xcb ] && [ -z "${DISPLAY:-}" ]; then
    echo "MediaForge: не найден графический дисплей (переменная DISPLAY пуста)." >&2
    echo "Запустите программу из графического сеанса." >&2
fi

exec "$HERE/bin/mediaforge" "$@"
