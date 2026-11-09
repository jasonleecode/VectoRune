"""Check actual IMG restrictions against routes, independently of the C++ matcher.

Usage: python3 test_garmin_routing.py route-helper imported/routing.json
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

helper, source = map(Path, sys.argv[1:3])
compiled = Path(sys.argv[3]) if len(sys.argv) > 3 else source
network = json.loads(source.read_text())
assert network["complete"]


def key(point):
    return tuple(round(v * 16777216 / 360) for v in point)


def contains(route, restriction):
    points = list(map(key, route.get("coordinates", [])))
    roads = route.get("roadIds", [])
    targets = list(map(key, restriction["nodes"]))
    for start, point in enumerate(points):
        if point != targets[0]:
            continue
        index = start
        for road, target in zip(restriction["roads"], targets[1:]):
            while index < len(roads) and roads[index] == road:
                index += 1
                if points[index] == target:
                    break
            else:
                break
            if points[index] != target:
                break
        else:
            return True
    return False


def plan(path, start, finish):
    result = subprocess.run([str(helper.resolve()), str(path), str(start[1]), str(start[0]),
                             str(finish[1]), str(finish[0]), "car", "shortest"],
                            capture_output=True, timeout=60)
    assert result.returncode in (0, 3), result.stderr
    return json.loads(result.stdout)


with tempfile.TemporaryDirectory(prefix="garmin-routing-test-") as temporary:
    unrestricted = Path(temporary) / "unrestricted.json"
    unrestricted.write_text(json.dumps({**network, "restrictions": []}, separators=(",", ":")))
    checked = 0
    attempts = 0
    for restriction in network["restrictions"]:
        if restriction["exceptMask"] & 1 or len(restriction["roads"]) != 2:
            continue
        attempts += 1
        start, finish = restriction["nodes"][0], restriction["nodes"][-1]
        before = plan(unrestricted, start, finish)
        if contains(before, restriction):
            after = plan(compiled.resolve(), start, finish)
            assert not contains(after, restriction), restriction
            assert "error" in after or after["distance"] >= before["distance"] - 0.01
            checked += 1
            print(f"PASS actual turn {checked}: {before['distance']:.1f} m -> "
                  + ("no permitted route" if "error" in after else f"{after['distance']:.1f} m"), flush=True)
            if checked == 3:
                break
        if attempts == 20:
            break
    assert checked, "No actual forbidden turn was exercised"
    bad = Path(temporary) / "unknown-condition.json"
    bad.write_text(json.dumps({"complete": False, "ways": network["ways"][:1]}))
    assert "error" in plan(bad, [116.4, 39.9], [116.41, 39.91])
    print(f"PASS: {checked} real turn restrictions and unknown-condition refusal")
