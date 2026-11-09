"""Independent MVT decoder checks tile clipping, ring winding and holes."""
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def varint(b, p):
    result = 0
    for shift in range(0, 70, 7):
        c = b[p]; p += 1; result |= (c & 127) << shift
        if c < 128:
            return result, p
    raise AssertionError('invalid varint')


def fields(b):
    p = 0
    while p < len(b):
        tag, p = varint(b, p)
        if tag & 7 == 0:
            value, p = varint(b, p)
        elif tag & 7 == 2:
            n, p = varint(b, p); value = b[p:p+n]; assert len(value) == n; p += n
        else:
            raise AssertionError(tag)
        yield tag >> 3, value


def packed(b):
    p = 0
    while p < len(b):
        v, p = varint(b, p); yield v


def paths(b):
    it = iter(packed(b)); x = y = 0; result = []; part = None
    for command in it:
        op, count = command & 7, command >> 3
        if op == 7:
            assert count == 1; part.append(part[0]); continue
        assert op in (1, 2) and count
        for _ in range(count):
            dx, dy = next(it), next(it)
            x += (dx >> 1) ^ -(dx & 1); y += (dy >> 1) ^ -(dy & 1)
            assert -64 <= x <= 4160 and -64 <= y <= 4160, (x, y)
            if op == 1:
                part = []; result.append(part)
            part.append((x, y))
    return result


def area(ring):
    return sum(a[0]*b[1] - b[0]*a[1] for a, b in zip(ring, ring[1:])) / 2


with tempfile.TemporaryDirectory() as directory:
    subprocess.run([sys.argv[1], directory], check=True, timeout=30)
    layer, = [v for k, v in fields((Path(directory)/'tile.pbf').read_bytes()) if k == 3]
    decoded = list(fields(layer))
    assert dict((k, v) for k, v in decoded if k in (1, 5, 15)) == {1: b'garmin', 5: 4096, 15: 2}
    keys = [v.decode() for k, v in decoded if k == 3]
    values = [dict(fields(v))[1].decode() for k, v in decoded if k == 4]
    features = {}
    for k, v in decoded:
        if k != 2: continue
        feature = dict(fields(v)); tags = list(packed(feature[2]))
        properties = {keys[a]: values[b] for a, b in zip(tags[::2], tags[1::2])}
        features[properties['name']] = (feature[3], paths(feature[4]))
    assert features['point'] == (1, [[(300, 300)]])
    assert features['line'][1] == [[(-64, 500), (4160, 500)]]
    outer, hole = features['hole'][1]
    assert area(outer) > 0 and area(hole) < 0
    assert abs(area(outer)+area(hole) - 8_000_000) < 10_000
    pieces = features['split'][1]
    assert len(pieces) == 2 and all(area(p) > 0 for p in pieces)
    assert len(features) == 4
print('PASS: independent MVT geometry, buffer bounds, concave split, holes and winding')
