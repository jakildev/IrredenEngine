"""Execute the lighting normal route against inverse-camera geometry."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_function

ROOT = Path(__file__).resolve().parents[2]

PREAMBLE = r"""
#include <cmath>
#include <initializer_list>
struct vec3 {
    float x,y,z;
    vec3(float a,float b,float c):x(a),y(b),z(c){}
    vec3 operator-() const {return {-x,-y,-z};}
};
constexpr int kXFace=0,kYFace=1;
int decodeFlipRoute(int flip,int) {return flip;}
struct Options {int x;};
"""

CASES = r"""
int main() {
    if(!selected(17,17) || selected(18,17)) return 1;
    for(int step=0;step<32;++step) for(float delta:{-.00001f,0.f,.00001f})
    for(int main=0;main<2;++main) for(int detached=0;detached<2;++detached)
    for(int route=0;route<2;++route) for(int flip=0;flip<2;++flip)
    for(int slot=0;slot<3;++slot) {
        const float yaw=step*float(std::acos(-1.0)/16)+delta;
        const float residual=(step%8==0 ? delta : .2f);
        const vec3 actual=run(main,detached,route,flip,slot,yaw,residual);
        double expected[]={.25,-.375,.75};
        if(main && !detached && !route && residual!=0) {
            double view[]={0,0,0};view[slot]=flip ? 1 : -1;
            expected[0]=std::cos(double(yaw))*view[0]-std::sin(double(yaw))*view[1];
            expected[1]=std::sin(double(yaw))*view[0]+std::cos(double(yaw))*view[1];
            expected[2]=view[2];
        }
        if(std::abs(actual.x-expected[0])>1.e-6 ||
           std::abs(actual.y-expected[1])>1.e-6 ||
           std::abs(actual.z-expected[2])>1.e-6) return 2;
    }
}
"""


@unittest.skipUnless(shutil.which("c++"), "normal-frame oracle requires C++")
class LightingNormalFrameTest(unittest.TestCase):
    def test_normal_frame_and_route_mutations(self):
        cpu = (
            ROOT / "engine/prefabs/irreden/render/systems/system_lighting_to_trixel.hpp"
        ).read_text()
        assignment = re.search(r"frameData_\.normalOptions_\.x = ([^;]+);", cpu)
        self.assertIsNotNone(assignment)
        selector = (
            "bool selected(int entity,int perAxisCanvasEntity_) {return " + assignment[1] + ";}\n"
        )
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            root = ROOT / "engine/render/src/shaders" / directory
            source = (root / f"c_lighting_to_trixel.{suffix}").read_text()
            source = source.replace("frameData.", "").replace("voxelFrameData.", "")
            begin = source.index("if (normalOptions.x")
            end = source.index("\n    }", begin) + len("\n    }")
            block = source[begin:end]
            common = (root / f"ir_iso_common.{suffix}").read_text()
            helpers = "\n".join(
                extract_function(common, name) for name in ("faceOutwardNormal", "rotateYawZInv")
            )
            helpers = helpers.replace("float3", "vec3")
            variants = {
                "production": block,
                "wrong_yaw": block.replace("slot), visualYaw", "slot), -visualYaw"),
                "secondary_canvas": block.replace("normalOptions.x != 0 && ", ""),
                "per_axis": block.replace("perAxisRoute == 0 && ", ""),
                "lost_polarity": block.replace(
                    "worldNormal = -worldNormal", "worldNormal = worldNormal"
                ),
            }
            for name, body in variants.items():
                with (
                    self.subTest(backend=suffix, variant=name),
                    tempfile.TemporaryDirectory() as tmp,
                ):
                    wrapper = (
                        """
vec3 run(int main,bool detachedCanvas,int perAxisRoute,int encoded,int slot,
         float visualYaw,float residualYaw) {
    Options normalOptions{main};
    vec3 worldNormal(.25f,-.375f,.75f);
"""
                        + body
                        + "\nreturn worldNormal;\n}\n"
                    )
                    path = Path(tmp) / "oracle.cpp"
                    path.write_text(PREAMBLE + helpers + selector + wrapper + CASES)
                    exe = Path(tmp) / "oracle"
                    result = subprocess.run(
                        ["c++", "-std=c++17", str(path), "-o", str(exe)],
                        capture_output=True,
                        text=True,
                    )
                    self.assertEqual(result.returncode, 0, result.stderr)
                    result = subprocess.run([str(exe)], capture_output=True, text=True)
                    if name == "production":
                        self.assertEqual(result.returncode, 0)
                    else:
                        self.assertNotEqual(body, block)
                        self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
