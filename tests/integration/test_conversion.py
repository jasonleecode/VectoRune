"""Validate conversion orchestration without network access or a large PBF fixture."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

script = Path(__file__).resolve().parents[2] / "tools/convert_osm.py"
with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    source = root / "input with spaces.osm.pbf"
    source.write_bytes(b"fixture handled by fake converter")
    helper = root / "tilemaker"
    helper.write_text(f"#!{sys.executable}\n" + '''import json, sqlite3, sys
from pathlib import Path
args = sys.argv
config = json.loads(Path(args[args.index('--config') + 1]).read_text())
assert all('source' not in layer for layer in config['layers'].values())
db = sqlite3.connect(args[args.index('--output') + 1])
db.executescript("CREATE TABLE metadata(name,value);INSERT INTO metadata VALUES('format','pbf');"
                 "CREATE TABLE tiles(tile_data);INSERT INTO tiles VALUES('fixture');")
db.commit()
''')
    helper.chmod(0o755)
    output = root / "result.mbtiles"
    command = [sys.executable, str(script), str(source), str(output), "--tilemaker", str(helper)]
    result = subprocess.run(command, cwd=root, capture_output=True, timeout=30)
    assert result.returncode == 0, result.stderr.decode()
    assert output.is_file()
    before = output.read_bytes()
    assert subprocess.run(command, capture_output=True).returncode != 0
    assert output.read_bytes() == before
    helper.write_text(f"#!{sys.executable}\nraise SystemExit(7)\n")
    command[3] = str(root / "failed.mbtiles")
    assert subprocess.run(command, capture_output=True).returncode != 0
    assert not (root / "failed.mbtiles").exists()
    assert not list(root.glob("vectorune-convert-*"))
print("PASS: explicit paths, validation, no overwrite, failed conversion leaves no output")
