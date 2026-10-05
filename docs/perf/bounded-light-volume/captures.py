from pathlib import Path
import shutil
import subprocess
import sys

root = Path.cwd()
artifacts = Path('/tmp/codex-bounded-light-artifacts')
shots = root / 'build/creations/demos/lighting/save_files/screenshots'
variant = sys.argv[1]
cases = [
    ('domain', 'IRLightingEmissive', '--light-domain-matrix', 36),
    ('boundary', 'IRLightingEmissive', '--light-boundary-sweep', 5),
    ('hover', 'IRLightingEmissive', '--hover-sweep', 5),
    ('occluded', 'IRLightingOccludedBoundary', '--light-boundary-sweep', 5),
]
for name, target, flag, count in cases:
    before = set(shots.glob('*.png'))
    command = ['fleet-run', target, flag, '--auto-screenshot', '10', '--window-mode', 'offscreen']
    result = subprocess.run(command, cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (artifacts / f'{name}-{variant}.log').write_text(result.stdout)
    if result.returncode or 'RESULT=CLEAN' not in result.stdout:
        raise RuntimeError(f'{name}: incomplete run, see retained log')
    images = sorted(set(shots.glob('*.png')) - before)
    if len(images) != count:
        raise RuntimeError(f'{name}: expected {count}, got {len(images)}')
    dest = artifacts / f'{name}-{variant}'
    dest.mkdir(exist_ok=True)
    for image in images:
        shutil.copy2(image, dest / image.name)
    print(f'{name}-{variant}: CLEAN, {count} captures', flush=True)
