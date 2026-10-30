#!/usr/bin/env python3
"""Check FANUC extraction fixture integrity without running CNC programs."""
from pathlib import Path
import json, re, sys
root = Path(__file__).resolve().parent
items = json.loads((root / 'manifest.json').read_text(encoding='utf-8'))['items']
errors = []
paths = [x['path'] for x in items]
if len(paths) != len(set(paths)):
    errors.append('duplicate manifest paths')
for entry in items:
    path = root / entry['path']
    if not path.is_file():
        errors.append('missing ' + entry['path'])
        continue
    lines = path.read_text(encoding='utf-8').splitlines()
    if len(lines) != entry['lines']:
        errors.append('line count mismatch: ' + entry['path'])
    if entry['source_complete']:
        if not re.match(r'^O\d{4}\b', lines[0]) or '%' not in lines:
            errors.append('bad program boundary: ' + entry['path'])
        if '…' in '\n'.join(lines) or re.search(r'\.\.\.', '\n'.join(lines)):
            errors.append('apparent missing source in complete listing: ' + entry['path'])
print(f'{len(items)} fixtures: {sum(x["source_complete"] for x in items)} numbered complete, '
      f'{sum(not x["source_complete"] for x in items)} fragments')
for error in errors:
    print('ERROR: ' + error, file=sys.stderr)
sys.exit(bool(errors))
