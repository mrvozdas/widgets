#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${W11WIDGET_CHECK_BUILD_DIR:-"$project_dir/build-check"}

printf 'Verification date: %s\n' "$(TZ=Europe/Istanbul date '+%Y-%m-%d %H:%M:%S %Z')"
printf 'Source commit: %s\n' "$(git -C "$project_dir" rev-parse --short HEAD)"
if [ -n "$(git -C "$project_dir" status --porcelain)" ]; then
    printf '%s\n' 'Source state: working tree contains changes.'
fi

set -- -S "$project_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DW11WIDGET_BUILD_OVERLAY=ON
if [ -n "${W11WIDGET_QT_PREFIX:-}" ]; then
    set -- "$@" "-DCMAKE_PREFIX_PATH=$W11WIDGET_QT_PREFIX"
fi
cmake "$@"
cmake --build "$build_dir"
ctest --test-dir "$build_dir" --output-on-failure --no-tests=error

# Keep the framework-free prototype contract under the same local gate.
cmake -S "$project_dir/native" -B "$build_dir/native" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir/native"
ctest --test-dir "$build_dir/native" --output-on-failure --no-tests=error

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) ;;
    *) printf '%s\n' 'SKIP: Windows native integration and installer verification require Windows.' ;;
esac
printf '%s\n' 'PASS: compiler, portable component tests, QML component loading and native core contracts.'
