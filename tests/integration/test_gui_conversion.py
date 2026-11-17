"""Exercise the GUI's actual C++ conversion controller with the bundled tilemaker."""
import gzip
import json
import os
from pathlib import Path
import shutil
import sqlite3
import struct
import subprocess
import sys
import tempfile

driver, tilemaker, fixture = map(Path, sys.argv[1:4])
env = dict(os.environ, VECTORUNE_HELPER_DIR=str(tilemaker.resolve().parent))


def varint(data, pos):
    value = shift = 0
    while True:
        byte = data[pos]; pos += 1
        value |= (byte & 127) << shift
        if byte < 128:
            return value, pos
        shift += 7


def fields(data):
    pos = 0
    while pos < len(data):
        tag, pos = varint(data, pos)
        wire = tag & 7
        if wire == 0:
            value, pos = varint(data, pos)
        elif wire == 2:
            count, pos = varint(data, pos)
            value = data[pos:pos + count]; pos += count
        elif wire in (1, 5):
            count = 8 if wire == 1 else 4
            value = data[pos:pos + count]; pos += count
        else:
            raise AssertionError(wire)
        yield tag >> 3, value


def layer_names(tile):
    if tile.startswith(b'\x1f\x8b'):
        tile = gzip.decompress(tile)
    return {dict(fields(v))[1].decode() for k, v in fields(tile) if k == 3}


with tempfile.TemporaryDirectory(prefix='vectorune-conversion-test-') as directory:
    root = Path(directory)
    source = root / 'Monaco with spaces.osm.pbf'
    shutil.copyfile(fixture, source)

    def run(path, kind='mbtiles', groups='building', preset='dark', extra=(), ok=True, input_file=source):
        result = subprocess.run([str(driver), str(input_file), str(path), kind, groups, preset, '0', '14', *extra],
                                env=env, capture_output=True, text=True, timeout=130)
        assert (result.returncode == 0) == ok, (result.stdout, result.stderr)
        record = json.loads(result.stdout.strip().splitlines()[-1])
        assert not list(root.glob('.vectorune-convert-*')), 'temporary conversion files leaked'
        return record

    output = root / 'buildings.mbtiles'
    record = run(output)
    assert record['canOpen']
    with sqlite3.connect(output) as db:
        metadata = dict(db.execute('select name,value from metadata'))
        assert metadata['vectorune:style'] == 'dark'
        names = {v['id'] for v in json.loads(metadata['json'])['vector_layers']}
        assert 'building' in names and names <= {'building', 'housenumber'}, names
        seen = set()
        height_found = False
        for tile, in db.execute('select tile_data from tiles'):
            seen |= layer_names(tile)
            height_found |= b'render_height' in gzip.decompress(tile)
        assert 'building' in seen and seen <= {'building', 'housenumber'}, seen
        assert height_found, 'building height attributes were lost'
    style = json.loads(Path(str(output) + '.style.json').read_text())
    assert style['layers'][0]['paint']['background-color'] == '#202630'
    before = output.read_bytes()
    run(output, ok=False)
    assert output.read_bytes() == before

    roads = root / 'roads.mbtiles'
    run(roads, groups='transportation', preset='light')
    with sqlite3.connect(roads) as db:
        seen = set().union(*(layer_names(tile) for tile, in db.execute('select tile_data from tiles')))
        assert 'transportation' in seen and seen <= {'transportation', 'transportation_name'}, seen

    pmtiles = root / 'buildings.pmtiles'
    assert not run(pmtiles, kind='pmtiles')['canOpen']
    header = pmtiles.read_bytes()[:127]
    assert header[:8] == b'PMTiles\x03' and struct.unpack_from('<Q', header, 72)[0] > 0
    offset, length = struct.unpack_from('<QQ', header, 24)
    metadata = json.loads(gzip.decompress(pmtiles.read_bytes()[offset:offset+length]))
    assert {v['id'] for v in metadata['vector_layers']} <= {'building', 'housenumber'}

    xyz = root / 'directory tiles'
    assert not run(xyz, kind='xyz')['canOpen']
    assert (xyz/'metadata.json').is_file() and (xyz/'style.json').is_file()
    tiles = list(xyz.glob('*/*/*.pbf'))
    assert tiles
    assert set().union(*(layer_names(p.read_bytes()) for p in tiles)) <= {'building', 'housenumber'}
    assert not tiles[0].read_bytes().startswith(b'\x1f\x8b'), 'directory tiles must be directly servable'

    cancelled = root/'cancelled.mbtiles'
    assert '取消' in run(cancelled, extra=('--cancel',), ok=False)['status']
    assert not cancelled.exists() and not Path(str(cancelled)+'.style.json').exists()
    run(root/'nothing.mbtiles', groups='', ok=False)
    bad = root/'broken.osm.pbf'; bad.write_bytes(b'not a PBF')
    run(root/'failed.mbtiles', input_file=bad, ok=False)
    assert not (root/'failed.mbtiles').exists()
print('PASS: real MBTiles/PMTiles/XYZ conversion, selected layers, height attributes, styles, cancellation, failure cleanup and no overwrite')
