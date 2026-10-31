"""Start or stop this project's hidden local development server."""
import argparse
import json
import os
import pathlib
import secrets
import subprocess
import sys
import time
import urllib.request
import webbrowser

ROOT = pathlib.Path(__file__).resolve().parents[1]
URL = 'http://127.0.0.1:8765/'
RECORD = ROOT / '.local/simulator-server.json'

def status():
    try:
        with urllib.request.urlopen(URL + '__build', timeout=1) as response:
            return json.load(response)
    except (OSError, ValueError):
        return None

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['start', 'stop'])
    parser.add_argument('--no-browser', action='store_true')
    args = parser.parse_args()
    if args.action == 'stop':
        if not RECORD.exists() or not status():
            print('Simulator server is already stopped.')
            return
        record = json.loads(RECORD.read_text(encoding='utf-8'))
        request = urllib.request.Request(URL + '__shutdown', data=b'', headers={'Authorization':'Bearer ' + record['token']}, method='POST')
        with urllib.request.urlopen(request, timeout=3):
            pass
        print('Simulator server stopped.')
        return
    current = status()
    if current and current.get('kind') != 'pocket-firmware-simulator':
        raise RuntimeError('Port 8765 belongs to a different service.')
    if not current:
        RECORD.parent.mkdir(exist_ok=True)
        token = secrets.token_urlsafe(32)
        env = dict(os.environ, POCKET_SIMULATOR_TOKEN=token)
        flags = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
        with (RECORD.parent / 'simulator-server.log').open('wb') as output, (RECORD.parent / 'simulator-server-error.log').open('wb') as error:
            process = subprocess.Popen([sys.executable, str(ROOT / 'tools/run_simulator.py'), '--no-browser'], cwd=ROOT,
                env=env, stdout=output, stderr=error, creationflags=flags, start_new_session=os.name != 'nt')
        RECORD.write_text(json.dumps({'pid':process.pid, 'token':token})+'\n', encoding='utf-8')
        for _ in range(120):
            if status():
                break
            if process.poll() is not None:
                raise RuntimeError('Server exited. See .local/simulator-server-error.log.')
            time.sleep(.5)
        else:
            raise RuntimeError('Server startup timed out. See .local/simulator-server.log.')
    print('Pocket simulator:', URL)
    if not args.no_browser:
        webbrowser.open(URL)

if __name__ == '__main__':
    main()
