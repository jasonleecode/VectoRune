#!/usr/bin/env bash
# Build the Fcitx 4/5 input context for the exact Qt SDK used by Viewer.
set -euo pipefail
if [ "$#" -lt 1 ]; then
    echo "Usage: $0 /path/to/Qt/6.x/gcc_64 [build/deps]" >&2
    exit 1
fi
qt_prefix=$(cd -- "$1" && pwd)
deps_dir=${2:-build/deps}
mkdir -p "$deps_dir"
deps_dir=$(cd -- "$deps_dir" && pwd)
source_dir="$deps_dir/fcitx5-qt"
build_dir="$deps_dir/fcitx5-qt-build"
revision=c743b12e6780edf1dcfe9071531c80f050cacb95
if [ ! -d "$source_dir" ]; then
    git clone --depth 1 --branch 5.1.6 https://github.com/fcitx/fcitx5-qt.git "$source_dir"
fi
if [ "$(git -C "$source_dir" rev-parse HEAD)" != "$revision" ]; then
    echo "Expected fcitx5-qt 5.1.6 ($revision) in $source_dir" >&2
    exit 1
fi
cmake_args=()
if [ -n "${ECM_DIR:-}" ]; then cmake_args+=("-DECM_DIR=$ECM_DIR"); fi
cmake -S "$source_dir" -B "$build_dir" \
    "-DCMAKE_PREFIX_PATH=$qt_prefix" -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_QT4=OFF -DENABLE_QT5=OFF -DENABLE_QT6=ON \
    -DBUILD_ONLY_PLUGIN=ON -DENABLE_QT6_WAYLAND_WORKAROUND=OFF \
    "-DCMAKE_INSTALL_QT6PLUGINDIR=$deps_dir/fcitx5-qt-install/plugins" \
    "${cmake_args[@]}"
cmake --build "$build_dir" --target fcitx5platforminputcontextplugin-qt6 --parallel 4
echo "Configure Viewer with:"
echo "  -DVECTORUNE_QT_INPUTCONTEXT_PLUGIN=$build_dir/qt6/platforminputcontext/libfcitx5platforminputcontextplugin.so"
