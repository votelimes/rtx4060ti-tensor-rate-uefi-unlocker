"""Verify user-supplied firmware before staging it; never download firmware."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

parser = argparse.ArgumentParser()
parser.add_argument('directory', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parent
manifest = json.loads((root/'ASSETS-MANIFEST.json').read_text())
checked = []
for name, expected in manifest.items():
    source = args.directory/name
    data = source.read_bytes()
    if len(data) != expected['bytes'] or hashlib.sha256(data).hexdigest() != expected['sha256']:
        raise SystemExit(f'Asset mismatch: {name}; nothing copied')
    checked.append((name, data))
target = root/'USB/AD106-EXPERIMENT/ASSETS'
target.mkdir(parents=True, exist_ok=True)
for name, data in checked:
    destination = target/name
    destination.write_bytes(data)
    if destination.read_bytes() != data:
        raise SystemExit(f'Staged file verification failed: {name}')
print(f'Verified and staged {len(checked)} files in {target}')
