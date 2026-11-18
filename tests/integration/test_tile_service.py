"""Exercise the loopback MVT service, cache reuse and request validation."""
import concurrent.futures
import contextlib
import http.client
import json
import select
import socket
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
import urllib.parse
from pathlib import Path

with tempfile.TemporaryDirectory() as directory:
    subprocess.run([sys.argv[1], directory], check=True, capture_output=True, timeout=30)
    worker = subprocess.Popen([sys.argv[2], str(Path(directory)/'geometry.sqlite')], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        assert select.select([worker.stdout], [], [], 10)[0], 'server startup timed out'
        line = worker.stdout.readline()
        if not line:
            raise RuntimeError(worker.stderr.read().decode())
        url = json.loads(line)['url']
        assert url.startswith('http://127.0.0.1:')
        def fetch(path):
            client = urllib.request.build_opener(urllib.request.ProxyHandler({}))
            with client.open(path, timeout=10) as response:
                assert response.headers['Content-Type'] == 'application/vnd.mapbox-vector-tile'
                return response.read()
        expected = (Path(directory)/'tile.pbf').read_bytes()
        tile_url = url + '/14/8192/8192.pbf'
        assert fetch(tile_url) == expected
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            assert all(v == expected for v in pool.map(fetch, [tile_url]*8))
        # Hold a complete viewport's requests open at once. The old service
        # aborted the ninth connection before returning any HTTP response.
        address = urllib.parse.urlsplit(tile_url)
        with contextlib.ExitStack() as stack:
            clients = [stack.enter_context(socket.create_connection((address.hostname, address.port), timeout=10))
                       for _ in range(32)]
            for client in clients:
                client.sendall(('GET ' + address.path + ' HTTP/1.1\r\nHost: localhost\r\n').encode())
            for client in clients:
                client.sendall(b'\r\n')
            for client in clients:
                response = http.client.HTTPResponse(client)
                response.begin()
                assert response.status == 200
                assert response.read() == expected, 'burst response was truncated'
                response.close()
        for path, status in [('/../metadata.json',404),('/15/0/0.pbf',500),('/14/999999/0.pbf',500)]:
            try:
                fetch(url + path)
            except urllib.error.HTTPError as error:
                assert error.code == status
            else:
                raise AssertionError('invalid tile request accepted')
    finally:
        worker.terminate(); worker.communicate(timeout=10)
print('PASS: loopback startup, exact MVT response, 32 simultaneous requests, invalid path/zoom rejected')
