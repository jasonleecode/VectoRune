"""Independent MVT fixture checks for offline MBTiles place lookup."""
import gzip
import json
import sqlite3
import subprocess
import sys
import tempfile
from pathlib import Path


def varint(v):
    out = bytearray()
    while v > 127:
        out.append((v & 127) | 128)
        v >>= 7
    return bytes(out + bytes([v]))


def number(field, value):
    return varint(field << 3) + varint(value)


def blob(field, value):
    return varint((field << 3) | 2) + varint(len(value)) + value


with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / "fixture.mbtiles"
    db = sqlite3.connect(source)
    db.executescript("CREATE TABLE metadata(name TEXT,value TEXT);"
                     "INSERT INTO metadata VALUES('format','pbf');"
                     "CREATE TABLE tiles(zoom_level INTEGER,tile_column INTEGER,tile_row INTEGER,tile_data BLOB);")
    feature = blob(2, b"\x00\x00") + number(3, 1) + blob(4, varint(9) + varint(4096) + varint(4096))
    layer = number(15, 2) + blob(1, b"place") + blob(2, feature) + blob(3, b"name:latin") \
        + blob(4, blob(1, "天安门 Café".encode())) + number(5, 4096)
    tile = blob(3, layer)
    helper = str(Path(sys.argv[1]).resolve())

    def search(term):
        result = subprocess.run([helper, str(source), term], capture_output=True, timeout=30)
        return json.loads(result.stdout)

    for data in (tile, gzip.compress(tile)):
        db.execute("DELETE FROM tiles")
        db.execute("INSERT INTO tiles VALUES(0,0,0,?)", (data,))
        db.commit()
        rows = search("天安门")["items"]
        assert len(rows) == 1 and rows[0]["name"] == "天安门 Café"
        assert abs(rows[0]["latitude"]) < 1e-9 and abs(rows[0]["longitude"]) < 1e-9
        assert search("cafe")["items"]
        assert not search("not-present")["items"]
    db.execute("UPDATE tiles SET tile_data=?", (b"\x1a\xff",))
    db.commit()
    assert "error" in search("天安门")
    db.execute("UPDATE metadata SET value='png'")
    db.commit()
    assert "error" in search("天安门")
    print("PASS: raw/gzip MVT names, Unicode/diacritics, geographic conversion, no match, damaged/raster tiles")
