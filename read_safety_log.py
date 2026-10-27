"""Read-only decoder for the two-slot AD106 safety journal. No GPU access."""
import argparse
import hashlib
import json
import re
from pathlib import Path


def decode(data):
    if len(data) != 8192:
        raise ValueError('Expected an 8192-byte SAFETY.LOG; retain the original file for analysis')
    records = []
    for slot in range(2):
        block = data[slot * 4096:(slot + 1) * 4096]
        expected = b'SHA256=' + hashlib.sha256(block[:4000]).hexdigest().encode() + b'\nEND\n'
        if not block.startswith(b'AD106 SAFETY v0.2.0\n') or block[4000:4076] != expected:
            continue
        text = block[:4000].decode('ascii', errors='strict').rstrip()
        sequence = re.search(r'^sequence=([1-9][0-9]*)$', text, re.MULTILINE)
        if sequence:
            records.append({'slot': slot, 'sequence': int(sequence[1]), 'text': text})
    if not records:
        raise ValueError('Neither checkpoint has a valid header and SHA-256; do not infer completion')
    records.sort(key=lambda record: record['sequence'])
    return {'valid_slots': len(records), 'latest': records[-1], 'records': records}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('journal', type=Path)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    try:
        result = decode(args.journal.read_bytes())
    except (OSError, ValueError) as exc:
        parser.exit(1, str(exc) + '\n')
    print(json.dumps(result, indent=2) if args.json else
          f"Valid slots: {result['valid_slots']}/2\n{result['latest']['text']}")
