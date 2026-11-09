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
    network["complete"] = False
    source.write_text(json.dumps(network))
    result = subprocess.run([sys.argv[1], "--serve", str(source)], capture_output=True, timeout=30)
    assert result.returncode != 0 and "error" in json.loads(result.stdout)
print("PASS: repeated queries reuse graph, mode changes rebuild, invalid requests and unknown restrictions rejected")
