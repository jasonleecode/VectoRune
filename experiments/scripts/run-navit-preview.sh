#!/bin/sh
set -eu
project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
preview="$project_root/build/navit-preview"
if [ ! -x "$preview/Navit Preview.app/Contents/MacOS/navit" ]; then
    echo "Navit 本地预览尚未构建，参见 docs/navit-preview-2026-11-09.md。" >&2
    exit 1
fi
export NAVIT_PREFIX="$preview/install"
export NAVIT_LIBDIR="$preview/install/lib/navit"
export NAVIT_SHAREDIR="$preview/install/share/navit"
export NAVIT_USER_DATADIR="$preview/runtime/user"
export DYLD_LIBRARY_PATH="$preview/install/lib/navit${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
cd "$preview/runtime"
exec "$preview/Navit Preview.app/Contents/MacOS/navit" "$preview/runtime/navit.xml"
