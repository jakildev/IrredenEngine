import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

from PIL import Image, ImageChops

sys.path.insert(0, str(Path('scripts/perf').resolve()))
from surface_lighting_controls import cases, scene_arguments

root = Path('/tmp/grid-receiver-controls')
root.mkdir(exist_ok=False)
build = Path('/tmp/sun-overflow-engine-pair/guarded-depth/build')
shots = build / 'creations/demos/canvas_stress/save_files/screenshots'
rows = []
for name, pose in cases('grid-overlap').items():
    if not name.startswith('per-axis-'):
        continue
    pair = []
    for prepass in ['0', '1']:
        label = name + '-prepass' + prepass
        command = ['fleet-run', '--build-dir', str(build), '--timeout', '120',
                   'IRCanvasStress', *scene_arguments('grid-overlap'), *pose,
                   '--auto-screenshot', '180', '--sweep-frames', '1', '180',
                   '--window-mode', 'offscreen']
        environment = dict(os.environ, IR_PERAXIS_SURFACE_LIGHTING='1',
                           IR_PERAXIS_VISIBILITY_PREPASS=prepass,
                           IR_PERAXIS_VISIBILITY_STATS='1')
        start = time.time()
        result = subprocess.run(command, env=environment, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        (root / (label + '.log')).write_text(result.stdout)
        if result.returncode or 'RESULT=CLEAN' not in result.stdout:
            raise SystemExit(f'{label}: native run failed')
        fresh = [p for p in shots.glob('*.png') if p.stat().st_mtime >= start]
        if len(fresh) != 1:
            raise SystemExit(f'{label}: expected one fresh image, got {len(fresh)}')
        target = root / (label + '.png')
        shutil.copyfile(fresh[0], target)
        counters = re.findall(r'VISIBILITY-PROBE fragments=(\d+) retained=(\d+) rejected=(\d+) pixels=(\d+)', result.stdout)
        row = dict(case=name, prepass=prepass, command=command,
                   environment={k: environment[k] for k in ['IR_PERAXIS_SURFACE_LIGHTING',
                                   'IR_PERAXIS_VISIBILITY_PREPASS', 'IR_PERAXIS_VISIBILITY_STATS']},
                   image=target.name, sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
                   counters=None)
        if prepass == '1':
            if len(counters) != 1:
                raise SystemExit(f'{label}: missing prepass witness')
            values = list(map(int, counters[0]))
            if values[0] <= 0 or values[1] + values[2] != values[0]:
                raise SystemExit(f'{label}: invalid prepass accounting')
            row['counters'] = dict(zip(['fragments', 'retained', 'rejected', 'pixels'], values))
        elif counters:
            raise SystemExit(f'{label}: prepass unexpectedly active')
        rows.append(row)
        pair.append(target)
        (root / 'captures.json').write_text(json.dumps(rows, indent=2) + '\n')
        print(label, row['counters'], flush=True)
    a, b = (Image.open(p).convert('RGBA') for p in pair)
    diff = ImageChops.difference(a, b)
    changed = sum(any(pixel) for pixel in diff.getdata())
    rows[-1]['changed_rgba_pixels'] = changed
    (root / 'captures.json').write_text(json.dumps(rows, indent=2) + '\n')
    print(name, 'changed pixels', changed, flush=True)
    if changed:
        raise SystemExit(f'{name}: prepass changes rendered image')
