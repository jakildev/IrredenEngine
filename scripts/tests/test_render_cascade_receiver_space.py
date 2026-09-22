"""Execute bake-axis packing and both public shader receiver wrappers."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_function

ROOT = Path(__file__).resolve().parents[2]
COMPILER = shutil.which("c++")



PREAMBLE = r"""
#include <cmath>
using uint = unsigned;
struct vec3 {
    float x,y,z;
    constexpr vec3(float a):x(a),y(a),z(a) {}
    constexpr vec3(float a,float b,float c):x(a),y(b),z(c) {}
};
struct vec4 {float x,y,z,w; vec4(float a,float b,float c,float d):x(a),y(b),z(c),w(d) {}};
vec3 operator+(vec3 a,vec3 b) {return {a.x+b.x,a.y+b.y,a.z+b.z};}
vec3 operator*(vec3 a,float b) {return {a.x*b,a.y*b,a.z*b};}
float dot(vec3 a,vec3 b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
namespace IRMath {
enum class CardinalIndex {k0,k90,k180,k270};
CPU_ROTATION
}
struct FrameDataSun {
    vec4 sunDirection{0,0,1,0},sunBasisU{1,0,0,0},sunBasisV{0,1,0,0};
};
vec4 sunDirection{0,0,1,0},sunBasisU{1,0,0,0},sunBasisV{0,1,0,0};
const float kNormalBiasVoxels=.5f;
float recordedDepth;
vec3 recordedPosition{0};
float worldSunShadowFactorImpl(vec3 p,vec3,float depth,bool,vec4) {
    recordedPosition=p; recordedDepth=depth; return depth;
}
float worldSunShadowFactorImpl(vec3 p,vec3 n,float depth,
    const FrameDataSun&,const unsigned*,bool s,vec4 q) {
    return worldSunShadowFactorImpl(p,n,depth,s,q);
}
FrameDataSun pack(IRMath::CardinalIndex cardinalIndex) {
    struct {vec4 sunDirection_{0,0,1,0},sunBasisU_{1,0,0,0},sunBasisV_{0,1,0,0};} frameData_;
    CPU_PACK
    return {frameData_.sunDirection_,frameData_.sunBasisU_,frameData_.sunBasisV_};
}
"""

CASES = r"""
int main() {
    const vec3 normal{1,0,0}; const vec4 rotation{0,0,0,1};
    const float depths[]={-65.125f,-59.2f,-51.2f,-43.2f,-30.75f};
    const vec3 axes[]={{1,1,1},{-1,1,1},{-1,-1,1},{1,-1,1}};
    for(int cardinal=0;cardinal<4;++cardinal) {
        const FrameDataSun frame=pack(IRMath::CardinalIndex(cardinal));
        sunDirection=frame.sunDirection; sunBasisU=frame.sunBasisU; sunBasisV=frame.sunBasisV;
        if(sunDirection.w!=axes[cardinal].x ||
           sunBasisU.w!=axes[cardinal].y || sunBasisV.w!=1) return 1;
        for(float depth:depths) for(int density:{1,2,4,8}) {
            // Construct the same bake-prism point via an independently tabulated inverse rotation.
            const float x=21.125f,y=-7.75f,z=depth-x-y;
            const vec3 world[]={ {x,y,z},{-y,x,z},{-x,-y,z},{y,-x,z} };
            const vec3 position=world[cardinal];
            // A recovered point is in world units at every raster density.
            const vec3 recovered{position.x*density/density,
                position.y*density/density,position.z*density/density};
            SURFACE_CALL;
            if(std::abs(recordedDepth-depth)>1e-4f) return 2;
            if(std::abs(recordedPosition.x-position.x)>1e-4f) return 3;
            RASTER_CALL;
            if(std::abs(recordedDepth-depth)>1e-4f) return 4;
            if(std::abs(recordedPosition.x-position.x-.5f)>1e-4f) return 5;
        }
    }
    return 0;
}
"""


@unittest.skipUnless(COMPILER, "receiver-space controls require a C++ compiler")
class CascadeReceiverSpaceTest(unittest.TestCase):
    def test_bake_axis_and_public_receivers(self):
        math = (ROOT / "engine/math/include/irreden/ir_math.hpp").read_text()
        rotation = extract_function(math, "rotateCardinalZInv")
        bake = (
            ROOT / "engine/prefabs/irreden/render/systems/system_bake_sun_shadow_map.hpp"
        ).read_text()
        start = bake.index("        const vec3 cascadeDepthAxis =")
        end = bake.index("frameData_.sunBasisV_.w = cascadeDepthAxis.z;", start)
        pack = bake[start:end] + "frameData_.sunBasisV_.w = cascadeDepthAxis.z;"
        preamble = PREAMBLE.replace("CPU_ROTATION", rotation).replace("CPU_PACK", pack)
        for suffix, directory, vector in (("glsl", "", "vec3"), ("metal", "metal/", "float3")):
            root = ROOT / "engine/render/src/shaders" / directory
            projection = (root / f"ir_sun_projection.{suffix}").read_text()
            sampler = (root / f"ir_sun_shadow_sample.{suffix}").read_text()
            helper = extract_function(projection, "sunCascadeReceiverDepth")
            wrappers = "\n".join(
                extract_function(sampler, name)
                for name in ("worldSunShadowFactor", "worldSurfaceSunShadowFactor")
            )
            wrappers = wrappers.replace("constant ", "const ").replace("device ", "")
            wrappers = wrappers.replace("float3", "vec3").replace("float4", "vec4")
            helper = helper.replace(vector, "vec3")
            args = ",frame,nullptr" if suffix == "metal" else ""
            cases = CASES.replace(
                "SURFACE_CALL", "worldSurfaceSunShadowFactor(recovered,normal,rotation" + args + ")"
            )
            cases = cases.replace(
                "RASTER_CALL", "worldSunShadowFactor(recovered,normal" + args + ")"
            )
            variants = {
                "production": (helper, wrappers, 0),
                "world_depth_instead_of_prism": (
                    helper.replace(
                        "dot(worldPosition, worldDepthAxis)", "dot(worldPosition, vec3(1))"
                    ),
                    wrappers,
                    2,
                ),
                "subdivision_scaled_depth": (
                    helper.replace(
                        "dot(worldPosition, worldDepthAxis)",
                        "2 * dot(worldPosition, worldDepthAxis)",
                    ),
                    wrappers,
                    2,
                ),
                "biased_cascade_depth": (
                    helper,
                    wrappers.replace(
                        "sunCascadeReceiverDepth(pos3D,",
                        "sunCascadeReceiverDepth(pos3D + normal * kNormalBiasVoxels,",
                    ),
                    2,
                ),
            }
            for name, (formula, public, expected) in variants.items():
                with (
                    self.subTest(backend=suffix, variant=name),
                    tempfile.TemporaryDirectory() as tmp,
                ):
                    path = Path(tmp)
                    (path / "receiver.cpp").write_text(preamble + formula + public + cases)
                    build = subprocess.run(
                        [
                            COMPILER,
                            "-std=c++17",
                            "-include",
                            "initializer_list",
                            str(path / "receiver.cpp"),
                            "-o",
                            str(path / "receiver"),
                        ],
                        capture_output=True,
                        text=True,
                    )
                    self.assertEqual(build.returncode, 0, build.stderr)
                    run = subprocess.run([str(path / "receiver")], capture_output=True)
                    self.assertEqual(run.returncode, expected)


if __name__ == "__main__":
    unittest.main()
