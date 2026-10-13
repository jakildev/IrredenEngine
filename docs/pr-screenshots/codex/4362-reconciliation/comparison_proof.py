"""Reproduce pinned cardinal-receiver and edge-tolerance counterexamples.

Run from any IrredenEngine checkout containing both pinned commits:
  python3 comparison_proof.py --repo /path/to/IrredenEngine --output /tmp/receiver-proof

Only output-directory files are written; C++ adapters execute shader functions
from the pinned commits. No renderer build, GPU, or reference mutation is used.
"""

import argparse
import importlib
import json
import re
import subprocess
import sys
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--repo", type=Path, default=Path.cwd())
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=True)
refs = {
    "0e89e931c54ff4572300f2448ac25666e00e6e98": [
        "scripts/tests/shader_contract_helpers.py",
        "scripts/tests/test_render_per_axis_caster_position.py",
        "scripts/tests/test_render_cardinal_voxel_receiver.py",
        "engine/render/src/shaders/ir_iso_common.glsl",
        "engine/render/src/shaders/c_voxel_to_trixel_stage_1_body.glsl",
        "engine/render/src/shaders/ir_cardinal_voxel_receiver.glsl",
        "engine/render/src/shaders/metal/ir_iso_common.metal",
        "engine/render/src/shaders/metal/c_voxel_to_trixel_stage_1_body.metal",
        "engine/render/src/shaders/metal/ir_cardinal_voxel_receiver.metal",
    ],
    "fef7e27fd6cf6f1b8ff1abf416d077dd62d218e5": [
        "engine/render/src/shaders/ir_iso_common.glsl",
        "engine/render/src/shaders/metal/ir_iso_common.metal",
    ],
}
for ref, paths in refs.items():
    for path in paths:
        target = root / ref[:9] / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(
            subprocess.run(
                ["git", "-C", str(args.repo), "show", ref + ":" + path],
                capture_output=True,
                text=True,
                check=True,
            ).stdout
        )

sys.path.insert(0, str(root / "0e89e931c/scripts/tests"))
ours = importlib.import_module("test_render_cardinal_voxel_receiver")
cpp = importlib.import_module("test_render_per_axis_caster_position").cpp
extract_function = importlib.import_module("shader_contract_helpers").extract_function
results = {}
checks = r"""

int main() {
    const vec3 positions[] = {{0, 0, 0}, {-4, 3, -2}, {.30f, -.20f, .49f}};
    unsigned total[2] = {}, planeWrong[2] = {}, centersDiffer[2] = {}, outsideMicro[2] = {};
    float largest[2] = {};
    int printed[2][3][2] = {};
    for (int mode : {0, 1})
        for (int density : {1, 2, 4, 8, 16})
            for (int cardinal = 0; cardinal < 4; ++cardinal)
                for (int axis = 0; axis < 3; ++axis)
                    for (int positive : {0, 1})
                        for (vec3 position : positions)
                            for (ivec2 canvas : {ivec2(642, 722), ivec2(643, 723)})
                                for (vec2 camera :
                                     {vec2(0), vec2(3.75f, -2.25f), vec2(-.3f, .7f)}) {
                                    voxelRenderOptions = {mode, density};
                                    cardinalIndex = cardinal;
                                    slot = axis;
                                    faceId = worldFace(axis, positive, cardinal);
                                    riserFlip = positive;
                                    voxelPosition = {position, 1};
                                    frameCanvasOffset = camera;
                                    trixelCanvasOffsetZ1 = trixelOriginOffsetZ1(canvas);
                                    const int samples = mode ? density : 1;
                                    for (int u = 0; u < samples; ++u)
                                        for (int v = 0; v < samples; ++v) {
                                            zIdx = u * density + v;
                                            for (int h = 0; h < 2; ++h) {
                                                const int offsets[3][2][2] = {{{1, 1}, {1, 2}},
                                                                              {{0, 1}, {0, 2}},
                                                                              {{0, 0}, {1, 0}}};
                                                localId = {offsets[axis][h][0],
                                                           offsets[axis][h][1]};
                                                gl_LocalInvocationID.xy = localId;
                                                bothPolaritiesExposed =
                                                    mode && !positive && u == 0 && v == 0;
                                                taps.clear();
                                                produce();
                                                for (unsigned i = 0; i < taps.size(); ++i) {
                                                    const int sign =
                                                        i == 0 ? positive : 1 - positive;
                                                    const vec3 expected =
                                                        expectedCenter(position, mode, density,
                                                                       axis, sign, u, v, cardinal);
                                                    const auto tap = taps[i];
                                                    const vec3 center = cardinalVoxelSurfaceCenter(
                                                        tap.pixel, tap.encoded,
                                                        trixelCanvasOffsetZ1, frameCanvasOffset,
                                                        voxelRenderOptions, cardinal);
                                                    if (length(vec2(center.x - expected.x,
                                                                    center.y - expected.y)) >
                                                            1e-5f ||
                                                        std::abs(center.z - expected.z) > 1e-5f)
                                                        return 3;
                                                    const vec3 other =
                                                        trixelCanvasPixelToVoxelFaceSurface3D(
                                                            tap.pixel,
                                                            decodeDepthSingle(tap.encoded), axis,
                                                            trixelCanvasOffsetZ1, frameCanvasOffset,
                                                            voxelRenderOptions, cardinal);
                                                    const vec3 delta = rotateCardinalZInv(
                                                        other - center, (4 - cardinal) % 4);
                                                    const float peak =
                                                        std::max(std::abs(delta.x),
                                                                 std::max(std::abs(delta.y),
                                                                          std::abs(delta.z)));
                                                    ++total[mode];
                                                    if (peak > 1e-5f)
                                                        ++centersDiffer[mode];
                                                    if (std::abs(delta[axis]) > 1e-5f)
                                                        ++planeWrong[mode];
                                                    float halfSpan = .5f / samples;
                                                    for (int k = 0; k < 3; ++k)
                                                        if (k != axis &&
                                                            std::abs(delta[k]) > halfSpan + 1e-5f) {
                                                            ++outsideMicro[mode];
                                                            break;
                                                        }
                                                    largest[mode] = std::max(largest[mode], peak);
                                                    if (peak > 1e-5f &&
                                                        printed[mode][axis][positive]++ < 2)
                                                        std::printf(
                                                            "case mode=%d density=%d cardinal=%d "
                                                            "axis=%d positive=%d u=%d v=%d half=%d "
                                                            "dual=%d pixel=(%d,%d) rawDepth=%d "
                                                            "center=(%.9g,%.9g,%.9g) "
                                                            "other=(%.9g,%.9g,%.9g) "
                                                            "deltaView=(%.9g,%.9g,%.9g)\n",
                                                            mode, density, cardinal, axis, sign, u,
                                                            v, h, i, tap.pixel.x, tap.pixel.y,
                                                            decodeDepthSingle(tap.encoded),
                                                            center.x, center.y, center.z, other.x,
                                                            other.y, other.z, delta.x, delta.y,
                                                            delta.z);
                                                }
                                            }
                                        }
                                }
    for (int m = 0; m < 2; ++m)
        std::printf("SUMMARY mode=%d total=%u wrongPlane=%u differentCenter=%u outsideMicro=%u "
                    "maxDelta=%.9g\n",
                    m, total[m], planeWrong[m], centersDiffer[m], outsideMicro[m], largest[m]);
}
"""
for backend in ["glsl", "metal"]:
    source, _ = ours.harness(backend)
    source = source.replace(ours.CHECKS, ours.CHECKS[: ours.CHECKS.index("int main(")])
    path = (
        root
        / "fef7e27fd/engine/render/src/shaders"
        / ("metal" if backend == "metal" else "")
        / ("ir_iso_common." + backend)
    )
    shader = path.read_text()
    helper = extract_function(shader, "trixelCanvasPixelToVoxelFaceSurface3D")
    midpoint = re.search(
        r"^(?:const|constant) (?:vec3|float3) kCardinalFaceBlockRecoveryMid = [^;]+;",
        shader,
        re.MULTILINE,
    ).group()
    source += "\n" + cpp(midpoint.replace("constant ", "const ") + "\n" + helper) + checks
    target = root / (backend + ".cpp")
    target.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-O2", str(target), "-o", str(root / backend)], check=True)
    output = subprocess.run(
        [str(root / backend)], capture_output=True, text=True, check=True
    ).stdout
    (root / (backend + ".txt")).write_text(output)
    print(backend + "\n" + output)
    results[backend] = {
        "geometry": [
            dict(field.split("=", 1) for field in line.split()[1:])
            for line in output.splitlines()
            if line.startswith("SUMMARY")
        ]
    }

preamble = r"""

#include <cmath>
#include <cstdio>
#include <initializer_list>
using std::abs;
struct bvec2 {
    bool x, y;
};
struct vec2 {
    float x, y;
    explicit vec2(float a) : x(a), y(a) {}
    vec2(float a, float b) : x(a), y(b) {}
    bvec2 operator<(vec2 b) const { return {x < b.x, y < b.y}; }
    bvec2 operator>(vec2 b) const { return {x > b.x, y > b.y}; }
};
struct vec3 {
    float x, y, z;
};
bool any(bvec2 b) { return b.x || b.y; }
bvec2 lessThan(vec2 a, vec2 b) { return a < b; }
bvec2 greaterThan(vec2 a, vec2 b) { return a > b; }
"""
checks = r"""

int main() {
    const vec3 u{1, 0, 0}, v{0, 1, 0};
    for (float cornerX : {1.000025f, 1.5f}) {
        const vec3 corner{cornerX, 0, 2};
        const vec2 query(cornerX - .00005f, .5f);
        const float hit = sourceFaceRaySeparation(query, 3, corner, u, v);
        const float closed = closedSeparation(query, 3, corner, u, v);
        const int first = int(std::floor(cornerX)), tile = int(std::floor(query.x));
        std::printf("corner=%.9g query=%.9g u=%.9g closed=%.9g tolerance=%.9g faceFirstTile=%d "
                    "queryTile=%d\n",
                    cornerX, query.x, query.x - cornerX, closed, hit, first, tile);
        if (closed != -1 || hit != 1)
            return 1;
    }
    const float outside = 1.00005f;
    std::printf("outside upper edge=%.9g ulps_at_one=%.9g accepted=%.9g\n", outside,
                (outside - 1.f) / (std::nextafter(1.f, 2.f) - 1.f),
                sourceFaceRaySeparation(vec2(outside, .5f), 3, {0, 0, 2}, u, v));
    return 0;
}
"""
for backend in ["glsl", "metal"]:
    path = "engine/render/src/shaders/" + ("metal/" if backend == "metal" else "")

    def get(file, ref="fef7e27fd6cf6f1b8ff1abf416d077dd62d218e5"):
        return subprocess.run(
            ["git", "-C", str(args.repo), "show", ref + ":" + path + file + "." + backend],
            capture_output=True,
            text=True,
            check=True,
        ).stdout

    geometry = get("ir_projected_face")
    query = get("ir_sun_face_query_layout")
    funcs = "\n".join(
        extract_function(geometry, n)
        for n in ["projectedFaceDeterminant", "projectedFaceCoordinates"]
    )
    body = extract_function(query, "sourceFaceRaySeparation")
    strict = get("ir_sun_face_query_layout", "0e89e931c54ff4572300f2448ac25666e00e6e98")
    closed = extract_function(strict, "sourceFaceRaySeparation")
    closed = closed.replace("sourceFaceRaySeparation", "closedSeparation")
    tolerance = (
        re.search(
            r"^(?:const|constant) float kSourceFaceEdgeTolerance = [^;]+;", query, re.MULTILINE
        )
        .group()
        .replace("constant ", "const ")
    )
    source = preamble + cpp(tolerance + "\n" + funcs + body + closed) + checks
    target = root / (backend + "_edge.cpp")
    target.write_text(source)
    subprocess.run(
        ["c++", "-std=c++17", "-O2", str(target), "-o", str(root / (backend + "_edge"))], check=True
    )
    run = subprocess.run(
        [str(root / (backend + "_edge"))], capture_output=True, text=True, check=True
    )
    (root / (backend + "_edge.txt")).write_text(run.stdout)
    print(backend + "\n" + run.stdout)

    results[backend]["edge_query"] = [
        dict(field.split("=", 1) for field in line.split()) for line in run.stdout.splitlines()[:2]
    ]
    results[backend]["outside_upper_edge"] = run.stdout.splitlines()[2]
(root / "comparison-results.json").write_text(json.dumps(results, indent=2) + "\n")
