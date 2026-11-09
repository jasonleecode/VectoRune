#!/usr/bin/env python3
"""Reproducible local OSM PBF -> OpenMapTiles MBTiles conversion via tilemaker."""
import argparse
import json
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--tilemaker", type=Path, help="tilemaker executable (otherwise built submodule or PATH)")
    parser.add_argument("--config", type=Path)
    parser.add_argument("--process", type=Path)
    parser.add_argument("--aux-data", type=Path, help="root containing optional coastline/landcover shapefiles")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    source, destination = args.input.resolve(), args.output.resolve()
    if not source.is_file() or not source.name.lower().endswith(".osm.pbf"):
        parser.error("input must be an existing .osm.pbf file")
    if destination.exists():
        parser.error("output already exists; choose a new filename")
    if destination.suffix.lower() != ".mbtiles":
        parser.error("output must end in .mbtiles")
    executable = args.tilemaker
    if executable is None:
        executable = next((p for p in [root / "tilemaker/build/tilemaker", root / "tilemaker/tilemaker"]
                           if p.is_file() and os.access(p, os.X_OK)), None)
    if executable is None:
        executable = shutil.which("tilemaker")
    if executable is None:
        parser.error("build tools/tilemaker first or pass --tilemaker /path/to/tilemaker")
    executable = Path(executable).resolve()
    if not executable.is_file() or not os.access(executable, os.X_OK):
        parser.error("tilemaker executable is missing or not executable")
    resources = root / "tilemaker/resources"
    if not resources.is_dir():
        resources = root.parent / "share/vectorune/tilemaker"
    config = (args.config or resources / "config-openmaptiles.json").resolve()
    process = (args.process or resources / "process-openmaptiles.lua").resolve()
    if not config.is_file() or not process.is_file():
        parser.error("missing conversion configuration; initialize the tilemaker submodule")
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Same filesystem as destination: publish only after SQLite validation succeeds.
    with tempfile.TemporaryDirectory(prefix="vectorune-convert-", dir=destination.parent) as temporary:
        output = Path(temporary) / "map.mbtiles"
        configuration = json.loads(config.read_text())
        for name, layer in list(configuration.get("layers", {}).items()):
            if "source" not in layer:
                continue
            shape = ((args.aux_data or config.parent) / layer["source"]).resolve()
            if shape.is_file():
                layer["source"] = str(shape)
            elif args.config or args.aux_data:
                parser.error(f"missing auxiliary data: {shape}")
            else:
                del configuration["layers"][name]
                print(f"No auxiliary shapefile for {name}; using OSM data only", file=sys.stderr)
        resolved_config = Path(temporary) / "config.json"
        resolved_config.write_text(json.dumps(configuration))
        subprocess.run([str(executable), "--input", str(source), "--output", str(output),
                        "--config", str(resolved_config), "--process", str(process)], check=True,
                       cwd=process.parent)
        with sqlite3.connect(f"{output.as_uri()}?mode=ro", uri=True) as db:
            if db.execute("PRAGMA quick_check").fetchone() != ("ok",):
                raise RuntimeError("tilemaker produced a damaged database")
            if db.execute("SELECT value FROM metadata WHERE name='format'").fetchone() != ("pbf",):
                raise RuntimeError("output is not vector MBTiles")
            if not db.execute("SELECT 1 FROM tiles LIMIT 1").fetchone():
                raise RuntimeError("output has no tiles")
        # link() refuses to replace an output created concurrently.
        os.link(output, destination)
    print(destination)


if __name__ == "__main__":
    main()
