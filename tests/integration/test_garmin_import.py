"""Exercise the real Garmin parser and validate its GeoJSON adapter output."""
import collections
import json
import sqlite3
import subprocess
import sys
import tempfile
from pathlib import Path

helper, fixture = map(Path, sys.argv[1:3])
with tempfile.TemporaryDirectory(prefix="garmin-import-test-") as temporary:
    output = Path(temporary)
    subprocess.run([str(helper.resolve()), str(fixture.resolve()), str(output)],
                   check=True, capture_output=True, timeout=180)
    metadata = json.loads((output / "metadata.json").read_text())
    features = json.loads((output / "map.geojson").read_text())["features"]
    counts = collections.Counter(f["geometry"]["type"] for f in features)
    assert counts["Point"] == metadata["points"] > 0
    assert counts["LineString"] == metadata["lines"] > 0
    assert counts["Polygon"] == metadata["polygons"] > 0
    assert 115 < metadata["west"] < metadata["east"] < 118
    assert 39 < metadata["south"] < metadata["north"] < 42
    assert metadata["names"]["namedLines"] > 18000
    assert metadata["names"]["namedPoints"] > 34000
    assert metadata["names"]["namedPolygons"] > 28000
    assert any(f["properties"].get("name") == "JIHANLÙ" for f in features)
    network = json.loads((output / "routing.json").read_text())
    assert network["complete"] and metadata["routingRulesComplete"]
    assert len(network["ways"]) == 92479
    assert len(network["restrictions"]) == 602
    assert (output / "routing.cbor").read_bytes().startswith(b"VRCB1\n")
    with sqlite3.connect(output / "places.sqlite") as places:
        assert places.execute("SELECT count(*) FROM places").fetchone()[0] == counts["Point"]
        assert places.execute("SELECT count(*) FROM positions").fetchone()[0] == counts["Point"]
        assert places.execute("SELECT count(*) FROM grams WHERE gram='ti'").fetchone()[0] > 0
    assert {len(r["roads"]) for r in network["restrictions"]} == {2, 3, 4, 5, 7}
    assert all(w["accessDecoded"] and w["roadId"] for w in network["ways"])
    assert any(w["accessMask"] & 1 for w in network["ways"])
    assert any(w["accessMask"] & 0x10 for w in network["ways"])
    assert any(w["accessMask"] & 0x20 for w in network["ways"])
    assert any(f["properties"].get("name") == "BEIJINGSHÌTONGJÌJÚ"
               and f["properties"].get("phone") == "+86 10 55535000"
               and f["properties"].get("house_number") == "209HÀOYUÀN"
               for f in features)
    for feature in features:
        geometry = feature["geometry"]
        if geometry["type"] == "Polygon":
            ring = geometry["coordinates"][0]
            assert len(ring) >= 4 and ring[0] == ring[-1]
        elif geometry["type"] == "LineString":
            assert len(geometry["coordinates"]) >= 2
    invalid = output / "invalid.img"
    invalid.write_bytes(b"This is not a Garmin map")
    result = subprocess.run([str(helper.resolve()), str(invalid), str(output / "bad")],
                            capture_output=True, timeout=15)
    assert result.returncode == 2, result.stderr.decode(errors="replace")
    assert not (output / "bad" / "metadata.json").exists()
    print(f"PASS: {len(features):,} features, rings, bounds, pinyin labels, phone/address decoding, invalid IMG")
