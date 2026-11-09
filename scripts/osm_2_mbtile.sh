#!/bin/sh
# Compatibility wrapper. Paths are explicit; the caller's working directory is irrelevant.
exec python3 "$(dirname "$0")/../tools/convert_osm.py" "$@"
