"""Serve only the simulator directory on localhost; no hardware or network needed."""
import argparse
import functools
import http.server
import json
import os
import pathlib
import subprocess
import sys
import threading
import time
import webbrowser
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from build_simulator import source_digest

ROOT = pathlib.Path(__file__).resolve().parents[1] / 'simulator'
PROJECT = ROOT.parent
STATE = {'kind':'pocket-firmware-simulator','version':None,'building':False,'error':''}

class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()

    def do_GET(self):
        if self.path.split('?')[0] == '/__build':
            data = json.dumps(dict(STATE)).encode('utf-8')
            self.send_response(200)
            self.send_header('Content-Type', 'application/json; charset=utf-8')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        else:
            super().do_GET()

    def do_POST(self):
        token = os.environ.get('POCKET_SIMULATOR_TOKEN')
        if self.path == '/__shutdown' and token and self.headers.get('Authorization') == 'Bearer ' + token:
            self.send_response(204)
            self.end_headers()
            threading.Thread(target=self.server.shutdown, daemon=True).start()
        else:
            self.send_error(403)

def rebuild():
    STATE.update(building=True,error='')
    result = subprocess.run([sys.executable, str(PROJECT / 'tools/build_simulator.py')],
                            cwd=PROJECT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace')
    if result.returncode:
        STATE.update(error=result.stdout[-12000:],building=False)
        print('Simulator build failed; retaining previous successful firmware.', flush=True)
    else:
        info = json.loads((ROOT / 'build-info.json').read_text(encoding='utf-8'))
        STATE.update(version=info['version'],building=False,error='')
        print(result.stdout.strip(), flush=True)

def watch_sources():
    previous = source_digest()
    while True:
        time.sleep(.75)
        try:
            current = source_digest()
            if current != previous:
                previous = current
                rebuild()
        except Exception as error:
            STATE.update(building=False,error=str(error))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--no-browser', action='store_true')
    args = parser.parse_args()
    try:
        info = json.loads((ROOT / 'build-info.json').read_text(encoding='utf-8'))
    except (FileNotFoundError, ValueError):
        info = {}
    if info.get('sourceHash') != source_digest():
        rebuild()
    else:
        STATE['version'] = info['version']
    handler = functools.partial(Handler, directory=str(ROOT))
    server = http.server.ThreadingHTTPServer(('127.0.0.1', args.port), handler)
    url = f'http://127.0.0.1:{server.server_port}/'
    print('Pocket simulator:', url, flush=True)
    threading.Thread(target=watch_sources, daemon=True).start()
    if not args.no_browser:
        threading.Timer(.3, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()

if __name__ == '__main__':
    main()
