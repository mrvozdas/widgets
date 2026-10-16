#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${W11WIDGET_CHECK_BUILD_DIR:-"$project_dir/build-check"}

printf 'Verification date: %s\n' "$(TZ=Europe/Istanbul date '+%Y-%m-%d %H:%M:%S %Z')"
printf 'Source commit: %s\n' "$(git -C "$project_dir" rev-parse --short HEAD)"
if [ -n "$(git -C "$project_dir" status --porcelain)" ]; then
    printf '%s\n' 'Source state: working tree contains changes.'
fi

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        native_script="$project_dir/scripts/build-native.ps1"
        native_build="$build_dir/native"
        if command -v cygpath >/dev/null 2>&1; then
            native_script=$(cygpath -w "$native_script")
            native_build=$(cygpath -w "$native_build")
        fi
        powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$native_script" \
            -BuildDirectory "$native_build" \
            -VisualStudioDirectory "${W11WIDGET_VS_ROOT:-C:\BuildTools2022}"
        ;;
    *)
        cmake -S "$project_dir/native" -B "$build_dir/native" -G Ninja -DCMAKE_BUILD_TYPE=Release
        cmake --build "$build_dir/native"
        ctest --test-dir "$build_dir/native" --output-on-failure --no-tests=error
        printf '%s\n' 'SKIP: Windows platform tests and WinUI compilation require Windows.'
        ;;
esac

case "${W11WIDGET_CHECK_LEGACY_QT:-0}" in
    0) printf '%s\n' 'SKIP: legacy Qt verification is opt-in (W11WIDGET_CHECK_LEGACY_QT=1).' ;;
    1)
        legacy_build="$build_dir/legacy-qt"
        set -- -S "$project_dir" -B "$legacy_build" -G Ninja \
            -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
            -DW11WIDGET_BUILD_OVERLAY=ON
        if [ -n "${W11WIDGET_QT_PREFIX:-}" ]; then
            set -- "$@" "-DCMAKE_PREFIX_PATH=$W11WIDGET_QT_PREFIX"
        fi
        cmake "$@"
        cmake --build "$legacy_build"
        ctest --test-dir "$legacy_build" --output-on-failure --no-tests=error
        ;;
    *) printf '%s\n' 'W11WIDGET_CHECK_LEGACY_QT must be 0 or 1.' >&2; exit 2 ;;
esac
printf '%s\n' 'PASS: compilation and the enabled headless tests. GUI, packaging and installer tests are separate commands.'
