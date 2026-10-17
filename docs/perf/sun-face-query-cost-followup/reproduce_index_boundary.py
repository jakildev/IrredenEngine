"""Execute production index/query twins to expose a tile-membership mismatch.

This serial host diagnostic does not reproduce GPU rendering or establish the
cause of a visible artifact. Exit zero means both backends reproduced the
expected accepted-but-unindexed receiver.
"""

import argparse
import ast
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

CASES = r"""
vec3 storedVector(uint record, uint offset) {
    float value[3];
    for (uint i = 0; i < 3; ++i)
        std::memcpy(&value[i], sunDepthBuf.data + record + offset + i, sizeof(float));
    return {value[0], value[1], value[2]};
}

int main() {
    constexpr uint unwritten = 0xccccccccu;
    std::vector<uint> storage(kSourceFaceBufferWords, unwritten);
    sunDepthBuf = {storage.data(), kSourceFaceBufferWords};
    sunDepthBuf[kSourceFaceHeaderOffset] = 0;
    sunDepthBuf[kSourceFaceSpillCounterOffset] = 0;
    for (uint tile = 0; tile < kSourceFaceTileCount; ++tile) {
        sunDepthBuf[sourceFaceTileBase(tile)] = 0;
        sunDepthBuf[kSourceFaceSpillHeadOffset + tile] = 0xffffffffu;
    }
    const float faceX = std::nextafter(8.0f, 9.0f);
    const float receiverX = std::nextafter(8.0f, 7.0f);
    const vec3 corner{faceX, 0, 0}, u{1, 0, 0}, v{0, 1, 0};
    CALL_INDEX
    const vec2 sunUV{receiverX, .5f}, origin{0, 0}, texelSz{1, 1};
    QUERY_TILE
    const uint tileIndex = uint(tile.y) * kSourceFaceTilesPerAxis + uint(tile.x);
    const uint base = sourceFaceTileBase(tileIndex);
    const uint count = sunDepthBuf[base];
    bool found = false;
    for (uint i = 0; i < count && i < kSourceFaceTileCapacity; ++i)
        found = found || sunDepthBuf[base + 1u + i] == 0u;
    const uint record = kSourceFaceRecordOffset;
    const float separation = sourceFaceRaySeparation(
        sunUV, 2, storedVector(record, 0), storedVector(record, 3), storedVector(record, 6));
    const uint neighborBase = sourceFaceTileBase(1u);
    std::cout << std::setprecision(17)
              << "face_x=" << faceX << " receiver_x=" << receiverX
              << " separation=" << separation
              << " receiver_tile=" << tileIndex << " receiver_count=" << count
              << " receiver_complete=" << sourceFaceQueryComplete(count)
              << " receiver_contains_face=" << found
              << " receiver_first_word_unwritten=" << (sunDepthBuf[base + 1u] == unwritten)
              << " neighbor_count=" << sunDepthBuf[neighborBase]
              << " neighbor_first_id=" << sunDepthBuf[neighborBase + 1u]
              << " published_faces=" << sunDepthBuf[kSourceFaceHeaderOffset] << '\n';
    return separation == 2 && tileIndex == 0 && count == 0 && !found
        && sourceFaceQueryComplete(count) && sunDepthBuf[base + 1u] == unwritten
        && sunDepthBuf[neighborBase] == 1 && sunDepthBuf[neighborBase + 1u] == 0
        && sunDepthBuf[kSourceFaceHeaderOffset] == 1 ? 0 : 1;
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--ref", default="HEAD", help="Git revision; ignored with --current")
    parser.add_argument("--current", action="store_true", help="Read working-tree shader bodies")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    repo = args.repo.resolve()
    output = args.output or Path(tempfile.mkdtemp(prefix="sun-index-query-boundary-"))
    output.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which("c++")
    if not compiler:
        raise SystemExit("A C++ compiler is required")

    def read(path):
        if args.current:
            return (repo / path).read_text()
        return subprocess.check_output(
            ["git", "-C", str(repo), "show", f"{args.ref}:{path}"], text=True)

    harness = ast.parse(read("scripts/tests/test_render_source_face_index.py"))
    preamble = next(ast.literal_eval(node.value) for node in harness.body
                    if isinstance(node, ast.Assign)
                    and any(isinstance(target, ast.Name) and target.id == "PREAMBLE"
                            for target in node.targets))
    revision = "working-tree" if args.current else subprocess.check_output(
        ["git", "-C", str(repo), "rev-parse", args.ref], text=True).strip()
    print(f"source={revision}", flush=True)
    for backend, directory in (("glsl", ""), ("metal", "metal/")):
        prefix = f"engine/render/src/shaders/{directory}"
        geometry = read(f"{prefix}ir_projected_face.{backend}")
        layout = read(f"{prefix}ir_sun_face_query_layout.{backend}")
        layout = layout.replace(f'#include "ir_projected_face.{backend}"', geometry)
        indexer = read(f"{prefix}ir_sun_face_index.{backend}")
        sampler = read(f"{prefix}ir_sun_shadow_sample.{backend}")
        tile_line = re.search(r"const (?:ivec2|int2) tile = [^;]+;", sampler).group()
        call = ("indexSourceSunFace(corner, u, v);" if backend == "glsl" else
                "indexSourceSunFace(sunDepthBuf.data, corner, u, v, sunFrame);")
        shader = layout + "\n" + indexer + CASES.replace("CALL_INDEX", call).replace(
            "QUERY_TILE", tile_line)
        shader = (shader.replace("constant uint", "const uint")
                  .replace("constant FrameDataSun&", "const FrameDataSun&")
                  .replace("device atomic_uint*", "atomic_uint*")
                  .replace("out vec2 uv", "vec2 &uv")
                  .replace("thread float2 &uv", "vec2 &uv")
                  .replace("float2", "vec2").replace("float3", "vec3")
                  .replace("int2", "ivec2").replace(".xy", ".xy()"))
        path = output / f"{backend}.cpp"
        path.write_text("#include <iomanip>\n" + preamble + shader)
        binary = path.with_suffix("")
        subprocess.run([compiler, "-std=c++17", "-O2", "-ffp-contract=off",
                        str(path), "-o", str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        print(f"{backend}: {result.stdout.strip()}", flush=True)
        if result.returncode:
            raise SystemExit(f"{backend}: expected mismatch was not reproduced; {result.stderr}")
    print(f"Both production index/query twins reproduced the mismatch. Artifacts: {output}")


if __name__ == "__main__":
    main()
