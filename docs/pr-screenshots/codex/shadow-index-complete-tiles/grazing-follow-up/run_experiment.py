#!/usr/bin/env python3
"""Characterize projected-coordinate precision using the actual shader helpers.

This standalone diagnostic is deliberately outside the CI suite. It changes
neither shaders nor their tolerance and does not classify native screenshots.
"""

import argparse
import csv
import hashlib
import importlib
import io
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


CASES = r"""
#include <iomanip>
struct D3 { double x,y,z; };
D3 cross(D3 a,D3 b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
double planeSeparation(D3 point,D3 corner,D3 edgeU,D3 edgeV) {
    const auto n=cross(edgeU,edgeV);
    return (n.x*(point.x-corner.x)+n.y*(point.y-corner.y)+
            n.z*(point.z-corner.z))/n.z;
}
D3 promote(vec3 a) { return {a.x,a.y,a.z}; }
int main() {
    std::cout<<std::setprecision(17);
    std::cout<<"origin,determinant,midpoint_oracle,received_float_oracle,"
                "shader_separation,post_projection_shift,world_relative_first,"
                "shader_false_self_hit,relative_false_self_hit\n";
    for(float origin:{0.0f,1.0f,16.0f,100.0f,1000.0f,-100.0f}) {
        for(float determinant:{1.0f,.1f,.01f,.001f,.0001f,.00005f,.00001f}) {
            const vec3 corner{origin,100,100};
            const vec3 u{determinant,0,std::sqrt(1.0f-determinant*determinant)};
            const vec3 v{0,1,0};
            const D3 intended{
                double(corner.x)+.5*double(u.x),
                double(corner.y)+.5*double(v.y),
                double(corner.z)+.5*double(u.z)
            };
            const vec3 received{float(intended.x),float(intended.y),float(intended.z)};
            const double oracle=planeSeparation(intended,promote(corner),promote(u),promote(v));
            const double roundedOracle=planeSeparation(promote(received),promote(corner),promote(u),promote(v));
            const float result=sourceFaceRaySeparation(
                {received.x,received.y},received.z,corner,u,v);
            const float shifted=sourceFaceRaySeparation(
                {received.x-corner.x,received.y-corner.y},received.z-corner.z,{0,0,0},u,v);
            // World face tangents are X/Y. Its exact relative midpoint is (.5,.5,0).
            const vec3 localProjection{.5f*u.x,.5f*v.y,.5f*u.z};
            const float relative=sourceFaceRaySeparation(
                {localProjection.x,localProjection.y},localProjection.z,{0,0,0},u,v);
            const bool falseHit=std::abs(oracle)<=kShadowBiasQuantNoise &&
                                result>kShadowBiasQuantNoise;
            const bool relativeFalseHit=std::abs(oracle)<=kShadowBiasQuantNoise &&
                                        relative>kShadowBiasQuantNoise;
            std::cout<<origin<<','<<determinant<<','<<oracle<<','<<roundedOracle<<','
                     <<result<<','<<shifted<<','<<relative<<','<<falseHit<<','
                     <<relativeFalseHit<<'\n';
        }
    }
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path(__file__).with_name("results.json"))
    args = parser.parse_args()
    repo = args.repo.resolve()
    sys.path.insert(0, str(repo / "scripts/tests"))
    harness = importlib.import_module("test_render_source_face_overflow")
    compiler = shutil.which("c++")
    if compiler is None:
        parser.error("a C++17 compiler is required")
    result = {
        "revision": subprocess.check_output(
            ["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip(),
        "scope": "CPU translation of actual shader helpers; no native attribution",
        "compiler": compiler,
        "flags": ["-std=c++17", "-O2", "-ffp-contract=off"],
        "bias": 4 / 1024,
        "backends": {},
    }
    for suffix, folder in (("glsl", ""), ("metal", "metal/")):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = harness.host_source(suffix, folder, False, cases=CASES)
            (directory / "witness.cpp").write_text(source)
            subprocess.run(
                [compiler, *result["flags"], str(directory / "witness.cpp"),
                 "-o", str(directory / "witness")], check=True, capture_output=True, text=True)
            output = subprocess.check_output([str(directory / "witness")], text=True)
        rows = [{key: float(value) for key, value in row.items()}
                for row in csv.DictReader(io.StringIO(output))]
        files = [repo / "engine/render/src/shaders" / folder / f"{name}.{suffix}"
                 for name in ("ir_projected_face", "ir_sun_face_query_layout")]
        result["backends"][suffix] = {
            "source_sha256": {str(path.relative_to(repo)): hashlib.sha256(
                path.read_bytes()).hexdigest() for path in files},
            "cases": rows,
            "false_self_hits": sum(bool(row["shader_false_self_hit"]) for row in rows),
            "relative_false_self_hits": sum(bool(row["relative_false_self_hit"]) for row in rows),
        }
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    for backend, data in result["backends"].items():
        print(f"{backend}: {len(data['cases'])} cases; "
              f"{data['false_self_hits']} projected false self hits; "
              f"{data['relative_false_self_hits']} relative-first false self hits")
    print(args.output)


if __name__ == "__main__":
    main()
