#!/usr/bin/env python3
"""Plan by default; --run performs eight bounded native A/B capture pairs.

Run from the dedicated worktree with GPU ownership already coordinated. The
existing single-pixel probe prints six decimal normalized depth and one decimal
raw distance: equality is diagnostic, not a proof of bitwise attachment equality.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path('/Users/evinjkill/src/IrredenEngine/.claude/worktrees/codex-shadow-render')
PAIR = Path('/tmp/sun-overflow-engine-pair')
EXE = 'build/creations/demos/canvas_stress/IRCanvasStress'
COMMON = [
    '--only', 'shadowbox,floor', '--probe-source-box',
    '--probe-floor-mode', 'grid', '--probe-floor-span', '40',
    '--sun-direction', '0', '0', '-1', '--no-spin', '--no-auto-rotate',
    '--pivot-origin', '--camera-iso', '0', '0', '--zoom', '4',
    '--subdivisions', '1', '--no-ao', '--auto-screenshot', '10',
    '--sweep-frames', '1', '10', '--frozen-pose', '0',
]
SCENES = {
    'floor-touch': ['--probe-box-offset', '0', '0', '10'],
    'floor-intersect': ['--probe-box-offset', '0', '0', '11'],
    'source-upper-intersect': [
        '--probe-box-offset', '0', '0', '10', '--probe-overlapping-source',
        '--probe-overlapping-source-count', '2',
        '--probe-overlapping-source-offset', '0', '0', '8',
    ],
    'source-side-intersect': [
        '--probe-box-offset', '0', '0', '10', '--probe-overlapping-source',
        '--probe-overlapping-source-count', '1',
        '--probe-overlapping-source-offset', '-8', '0', '12',
    ],
}
CASES = {f'{name}-yaw{yaw}': [*flags, '--yaw', yaw]
         for name, flags in SCENES.items() for yaw in ('0', '0.6')}
DEPTH_RE = re.compile(
    r'\[depth-probe\] pixel=\((\d+),(\d+)\) normDepth=([-\d.]+) '
    r'rawDist=([-\d.]+) enc=(-?\d+) tier=(\d+) \((\w+)\) '
    r'iso=(-?\d+) face=(-?\d+)'
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2) + '\n')


def verify_artifacts():
    artifacts = {}
    for name in ('guarded-depth', 'forced-fallback'):
        folder = PAIR / name
        manifest = json.loads((folder / 'artifact.json').read_text())
        for relative, expected in manifest['files'].items():
            if sha(folder / relative) != expected:
                raise RuntimeError(f'Artifact drift: {name}/{relative}')
        source = json.loads((folder / 'source/manifest.json').read_text())
        for relative, expected in source['files'].items():
            if sha(folder / 'source' / relative) != expected:
                raise RuntimeError(f'Source provenance drift: {name}/{relative}')
        artifacts[name] = {
            'path': str(folder), 'manifest': manifest,
            'manifest_sha256': sha(folder / 'artifact.json'),
            'source_manifest_sha256': sha(folder / 'source/manifest.json'),
            'executable_sha256': sha(folder / EXE),
        }
    baseline = artifacts['guarded-depth']['manifest']['files']
    control = artifacts['forced-fallback']['manifest']['files']
    if set(baseline) != set(control):
        raise RuntimeError('Artifact file sets differ')
    changed = sorted(path for path in baseline if baseline[path] != control[path])
    expected = sorted([
        'build/creations/demos/canvas_stress/shaders/ir_source_face_depth.glsl',
        'build/creations/demos/canvas_stress/shaders/metal/ir_source_face_depth.metal',
    ])
    if changed != expected:
        raise RuntimeError(f'Unexpected runtime differences: {changed}')
    return artifacts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true', help='Launch fleet-run captures')
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--output', type=Path, default=Path('/tmp/shadow-depth-rounding-controls'))
    parser.add_argument('--case', action='append', choices=CASES)
    parser.add_argument('--probe-pixel', default='640,360',
                        help='Main framebuffer texel, NOT screenshot pixel')
    args = parser.parse_args()
    if not re.fullmatch(r'\d+,\d+', args.probe_pixel):
        parser.error('--probe-pixel must be X,Y')
    names = list(dict.fromkeys(args.case or CASES))
    artifacts = verify_artifacts()
    source = args.root / 'creations/demos/canvas_stress/main.cpp'
    demo_text = source.read_text()
    all_flags = COMMON + [flag for name in names for flag in CASES[name]] + ['--depth-probe']
    for flag in {flag for flag in all_flags if flag.startswith('--')}:
        if f'"{flag}"' not in demo_text:
            raise RuntimeError(f'Demo flag not found in source: {flag}')
    provenance = {
        'driver_sha256': sha(Path(__file__)), 'artifacts': artifacts,
        'cli_source': str(source), 'cli_source_sha256': sha(source),
        'cases': {name: [*COMMON, *CASES[name], '--depth-probe', args.probe_pixel]
                  for name in names},
        'depth_precision_limit': 'Printed normDepth 6 decimals, rawDist 1 decimal; not bitwise depth verification.',
        'probe_coordinate_space': 'Main composite framebuffer texture, top-left origin. Pixel640,360 empirically verified non-background; canvas trixel extent642x722 is not framebuffer extent.',
        'geometry': 'Blue 18x6x8 box center z=-2 touches GRID floor at z=4 with thickness 4; z=-1 intersects it. Orange 3x3x3 source boxes rotate 0.47 about normalized (1,1,1).',
        'performance_limit': 'Depth probe flushes every frame; these captures are not timing measurements.',
        'cases_run': {}, 'comparisons': {},
    }
    if not args.run:
        print(json.dumps({
            'status': 'READY; no native launch requested',
            'paired_cases': len(names), 'native_launches': 2 * len(names),
            'probe_pixel': args.probe_pixel, 'output': str(args.output),
            'cases': provenance['cases'],
            'executable_sha256': artifacts['guarded-depth']['executable_sha256'],
        }, indent=2))
        return 0

    sys.path.insert(0, str(args.root / 'scripts'))
    from verify_common import collect_full_frames, platform_launch_argv, run_result
    from PIL import Image, ImageChops, ImageStat

    args.output.mkdir(parents=True, exist_ok=False)
    write_json(args.output / 'captures.json', provenance)
    differences = False
    for name in names:
        frames, depths = {}, {}
        for variant in ('guarded-depth', 'forced-fallback'):
            folder = args.output / name / variant
            folder.mkdir(parents=True)
            config = folder / 'config.lua'
            config.write_text(
                'config={window_mode="offscreen",profiling_enabled=false,'
                'gpu_stage_timing=false,init_window_width=1280,init_window_height=720,'
                'game_resolution_width=1280,game_resolution_height=720,'
                'screenshot_output_dir=' + json.dumps(str(folder)) + '}\n'
            )
            overrides = {'IRREDEN_BUILD_DIR': str(PAIR / variant / 'build'),
                         'IR_PERAXIS_SURFACE_LIGHTING': '1'}
            command = ['fleet-run', '--timeout', '180', 'IRCanvasStress',
                       '--config-preset', str(config), *provenance['cases'][name]]
            with (folder / 'run.log').open('w') as stream:
                result = subprocess.run(platform_launch_argv(command), cwd=args.root,
                                        env={**os.environ, **overrides}, stdout=stream,
                                        stderr=subprocess.STDOUT)
            log = (folder / 'run.log').read_text()
            shots = collect_full_frames(folder)
            records = [dict(zip(
                ('x', 'y', 'normDepth', 'rawDist', 'enc', 'tier', 'kind', 'iso', 'face'),
                match.groups(), strict=True)) for match in DEPTH_RE.finditer(log)]
            (folder / 'depth-samples.log').write_text('\n'.join(
                line[line.index('[depth-probe]'):] for line in log.splitlines()
                if '[depth-probe]' in line) + '\n')
            write_json(folder / 'depth-samples.json', records)
            if result.returncode != 0 or run_result(log) != 'CLEAN' or len(shots) != 1:
                raise RuntimeError(f'Invalid capture {name}/{variant}; inspect {folder}/run.log')
            if not records or float(records[-1]['normDepth']) >= 0.999999:
                raise RuntimeError(f'Missing/background depth sample {name}/{variant}; inspect pixel selection')
            frames[variant] = Image.open(shots[0]).convert('RGB')
            depths[variant] = records[-1]
            provenance['cases_run'][f'{name}/{variant}'] = {
                'command': command, 'environment': overrides, 'result': 'CLEAN',
                'frame': str(shots[0].relative_to(args.output)), 'frame_sha256': sha(shots[0]),
                'depth_sample_count': len(records), 'last_depth': records[-1],
                'log_sha256': sha(folder / 'run.log'),
            }
            write_json(args.output / 'captures.json', provenance)
            print(f'{name}/{variant}: CLEAN; depth {records[-1]["normDepth"]}', flush=True)

        a, b = frames['guarded-depth'], frames['forced-fallback']
        if a.size != b.size:
            raise RuntimeError(f'Capture sizes differ for {name}')
        diff = ImageChops.difference(a, b)
        bbox = diff.getbbox()
        changed = sum(pixel != (0, 0, 0) for pixel in diff.getdata())
        diff.save(args.output / name / 'rgb-difference.png')
        diff.point(lambda value: min(value * 8, 255)).save(args.output / name / 'rgb-difference-x8.png')
        depth_equal = depths['guarded-depth'] == depths['forced-fallback']
        comparison = {'changed_pixels': changed, 'changed_bbox': bbox,
                      'maximum_channel_delta': max(high for low, high in diff.getextrema()),
                      'mean_absolute_channel_delta': ImageStat.Stat(diff).mean,
                      'last_printed_depth_equal': depth_equal, 'size': a.size}
        provenance['comparisons'][name] = comparison
        differences |= changed != 0 or not depth_equal
        write_json(args.output / 'captures.json', provenance)
        print(f'{name}: {json.dumps(comparison)}', flush=True)
    verify_artifacts()
    provenance['result'] = 'DIFFERENCES' if differences else 'IDENTICAL_RGB_AND_PRINTED_DEPTH'
    write_json(args.output / 'captures.json', provenance)
    return 1 if differences else 0


if __name__ == '__main__':
    raise SystemExit(main())
