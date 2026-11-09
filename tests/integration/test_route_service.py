"""Black-box worker protocol test, including mode changes and repeated requests."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / "routing.json"
    network = {"complete": True, "restrictions": [], "ways": [{
        "coordinates": [[116, 40], [116.001, 40], [116.002, 40]],
        "junctions": [0, 1, 2], "hasJunctionFlags": True, "accessDecoded": True,
        "accessMask": 0, "roadId": "test", "type": 6, "name": "test"}]}
    source.write_text(json.dumps(network))
    request = {"start": [116, 40], "finish": [116.002, 40], "mode": "car", "strategy": "shortest"}
    requests = [request, {**request, "strategy": "fastest"}, {**request, "mode": "walk"},
                {**request, "mode": "flying"}, request]
    result = subprocess.run([sys.argv[1], "--serve", str(source)],
        input="".join(json.dumps(r) + "\n" for r in requests), text=True, capture_output=True, timeout=30)
    assert result.returncode == 0, result.stderr
    rows = [json.loads(line) for line in result.stdout.splitlines()]
    assert len(rows) == 5 and [rows[i]["graphBuilds"] for i in [0, 1, 2, 4]] == [1, 1, 2, 3]
    assert rows[0]["coordinates"] == rows[1]["coordinates"] and "error" in rows[3]
    compiled = Path(directory) / "compiled"
    subprocess.run([sys.argv[1], "--compile", str(source), str(compiled)], check=True, capture_output=True)
    result = subprocess.run([sys.argv[1], "--serve", str(compiled / "routing-index.json")],
        input="".join(json.dumps(r) + "\n" for r in requests), text=True, capture_output=True, timeout=30)
    indexed_rows = [json.loads(line) for line in result.stdout.splitlines()]
    for i in [0, 1, 2, 4]:
        assert indexed_rows[i]["graphBuilds"] == 0
        assert indexed_rows[i]["graphLoads"] == rows[i]["graphBuilds"]
        assert indexed_rows[i]["coordinates"] == rows[i]["coordinates"]
        assert indexed_rows[i]["distance"] == rows[i]["distance"]
    graph = compiled / "car.vgr"
    # A mismatched mode must not remain resident after a failed load.
    (compiled / "walk.vgr").write_bytes((compiled / "bicycle.vgr").read_bytes())
    result = subprocess.run([sys.argv[1], "--serve", str(compiled / "routing-index.json")],
        input="".join(json.dumps(r) + "\n" for r in [request, {**request, "mode": "walk"}, request]),
        text=True, capture_output=True, timeout=30)
    recovered = [json.loads(line) for line in result.stdout.splitlines()]
    assert "error" in recovered[1] and recovered[2]["graphLoads"] == 2
    assert recovered[2]["coordinates"] == recovered[0]["coordinates"]
    payload = bytearray(graph.read_bytes()); payload[-1] ^= 1; graph.write_bytes(payload)
    broken = subprocess.run([sys.argv[1], "--serve", str(compiled / "routing-index.json")],
        input=json.dumps(request) + "\n", text=True, capture_output=True, timeout=30)
    assert "error" in json.loads(broken.stdout)
    network["complete"] = False
    source.write_text(json.dumps(network))
    result = subprocess.run([sys.argv[1], "--serve", str(source)], capture_output=True, timeout=30)
    assert result.returncode != 0 and "error" in json.loads(result.stdout)
print("PASS: repeated queries reuse graph, mode changes rebuild, invalid requests and unknown restrictions rejected")
