"""Execute finite-query and fallback basis contracts with rotated caster planes.

The shared adapter runs the production sampler through its surface layers. These
controls do not exercise GPU uploads, index construction, legacy PCF, or pixels.
"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_function
from test_render_source_face_overflow import SHADERS, host_source

COMPILER = shutil.which("c++")

VECTOR_OPERATIONS = r"""
vec3 operator+(vec3 a,vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
vec3 operator-(vec3 a,vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
vec3 operator*(float a,vec3 b){return {a*b.x,a*b.y,a*b.z};}
vec3 cross(vec3 a,vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
"""

CASES = r"""
D3 scale(D3 p,double s){return {p.x*s,p.y*s,p.z*s};}
D3 unit(D3 p){return scale(p,1.0/std::sqrt(dot(p,p)));}
vec3 toFloat(D3 p){return {float(p.x),float(p.y),float(p.z)};}
D3 rotateEuler(D3 p,double yaw,double pitch){
    // Independent ZX matrix, rather than the shader's quaternion cross-product formula.
    const double y=std::cos(pitch)*p.y-std::sin(pitch)*p.z;
    const double z=std::sin(pitch)*p.y+std::cos(pitch)*p.z;
    return {std::cos(yaw)*p.x-std::sin(yaw)*y,
            std::sin(yaw)*p.x+std::cos(yaw)*y,z};
}
vec4 cameraQuaternion(double yaw,double pitch){
    return {float(std::cos(yaw/2)*std::sin(pitch/2)),
            float(std::sin(yaw/2)*std::sin(pitch/2)),
            float(std::sin(yaw/2)*std::cos(pitch/2)),
            float(std::cos(yaw/2)*std::cos(pitch/2))};
}
bool planeOracle(vec2 uv,float z,D3 normal,vec3 sun,vec3 u,vec3 v){
    const D3 rayOrigin=scale(toDouble(u),uv.x)+scale(toDouble(v),uv.y)
        -scale(toDouble(sun),z);
    const D3 planePoint=scale(toDouble(u),2.5)+scale(toDouble(v),3.5)
        -scale(toDouble(sun),10.0);
    const double distance=dot(normal,planePoint-rayOrigin)/dot(normal,toDouble(sun));
    // The receiver faces the sun, so its plane at the tap is z units deep.
    return distance>4.0/1024.0 && distance<100.0 && z-10.0>4.0/1024.0 && z-10.0<100.0;
}
int main(){
    const uint empty=0xffffffffu;
    int queries=0,hits=0,misses=0;
    for(uint cascade:{0u,1u}) {
        reset(65,65,cascade);
        for(double yaw:{.37,1.13,-.81}) for(double pitch:{.29,-.53}) {
            const vec4 camera=cameraQuaternion(yaw,pitch);
            for(int face=0;face<6;++face) for(int basis:{0,1}) {
                D3 normal{0,0,0};
                const double sign=(face&1)?1.0:-1.0;
                if(face/2==0)normal.x=sign;
                else if(face/2==1)normal.y=sign;
                else normal.z=sign;
                if(basis==1)normal=rotateEuler(normal,yaw,pitch);
                const D3 tangent=unit(cross(normal,abs(normal.z)<.9?D3{0,0,1}:D3{0,1,0}));
                const D3 bitangent=cross(normal,tangent);
                const vec3 sun=toFloat(unit(normal+scale(tangent,.65)+scale(bitangent,.45)));
                const vec3 u=toFloat(unit(cross(toDouble(sun),tangent)));
                const vec3 v=toFloat(cross(toDouble(sun),toDouble(u)));
                const uint packed=(uint((10+512)*1024)<<8)|sunVoxelFaceMarker(face,basis);
                fallback(cascade,empty,packed);
                for(float x:{2.1f,2.3f,2.7f,2.9f})
                for(float y:{3.1f,3.3f,3.7f,3.9f})
                for(float height:{.01f,.1f,.3f,1.f}) {
                    const vec2 uv{x,y};
                    const bool expected=planeOracle(uv,10+height,normal,sun,u,v);
                    const float actual=basisSample(uv,10+height,cascade,sun,u,v,camera);
                    if(actual!=float(expected)) {
                        std::cerr<<"fallback basis="<<basis<<" face="<<face<<" yaw="<<yaw
                            <<" pitch="<<pitch<<" expected="<<expected<<" actual="<<actual<<"\n";
                        return 1;
                    }
                    require(sunDepthBuf.readCount==0,"incomplete tile must not read face records");
                    ++queries;hits+=expected;misses+=!expected;
                }
            }
        }
        reset(1,1,cascade);
        sunDepthBuf.words[tileBase(cascade)+1]=0;
        writeFace({{2,3,10},{1,0,0},{0,1,0}});
        fallback(cascade,empty,depth(2));
        const vec4 camera=cameraQuaternion(.71,-.43);
        const auto exact=[&](vec2 uv,float z){
            return basisSample(uv,z,cascade,{0,0,1},{1,0,0},{0,1,0},camera);
        };
        require(exact({2.9f,3.5f},10)==0,"complete query rejects its own face");
        require(exact({2.9f,3.5f},10+4.f/1024)==0,"self tolerance is strict");
        require(exact({2.9f,3.5f},11)==1,"complete query accepts an external blocker");
        require(exact({3.1f,3.5f},11)==0,"complete miss ignores indexed fallback");
        sunDepthBuf.words[kSourceFaceHeaderOffset]=2;
        sunDepthBuf.words[tileBase(cascade)]=2;
        sunDepthBuf.words[tileBase(cascade)+2]=1;
        writeFace({{2,3,8},{1,0,0},{0,1,0}});
        require(exact({2.9f,3.5f},10)==1,"self record cannot hide an external blocker");
    }
    require(hits>1000 && misses>1000,"plane fixture must contain hits and misses");
    std::cout<<queries<<" fallback queries; "<<hits<<" hits; "<<misses<<" misses\n";
}
"""


def source(suffix, directory, mutation=None):
    code = host_source(suffix, directory, False, mutation)
    code = code[:code.index("int main(){")]
    common = (SHADERS / directory / f"ir_iso_common.{suffix}").read_text()
    projection = (SHADERS / directory / f"ir_sun_projection.{suffix}").read_text()
    constants = "\n".join(re.findall(r"^(?:const|constant) int kFace\w+ = [^;]+;",
                                    common, re.MULTILINE))
    normal = extract_function(common, "faceOutwardNormal6")
    rotation = extract_function(common, "rotateByQuat")
    rotation = rotation.replace("q.xyz", "vec3(q.x,q.y,q.z)")
    code = code.replace(extract_function(code, "faceOutwardNormal6"),
                        VECTOR_OPERATIONS + constants + "\n" + normal)
    code = code.replace(extract_function(code, "rotateByQuat"), rotation)
    call = ("sampleCascadeShadow(uv,z,sun,sun,u,v,{0,0},{1,1},"
            "int(cascade)*kCascadeTexelCount,")
    if suffix == "metal":
        call += "sunDepthBuf,"
    call += "100,true,camera,{0,0,0})"
    code += extract_function(projection, "sunVoxelFaceMarker")
    code += """
float basisSample(vec2 uv,float z,uint cascade,vec3 sun,vec3 u,vec3 v,vec4 camera){
    return """ + call + ";\n}\n" + CASES
    return (code.replace("constant int", "const int")
            .replace("float3", "vec3").replace("float4", "vec4"))


@unittest.skipUnless(COMPILER, "caster-basis controls require a C++ compiler")
class SourceFaceBasisTest(unittest.TestCase):
    def test_rotated_fallback_and_complete_queries(self):
        variants = {
            "production": None,
            "ignore_camera": ("rotateByQuat(planeNormal, casterViewToWorld)", "planeNormal"),
            "rotate_world_basis": ("if (sunVoxelFaceViewAligned(nearest))", "if (true)"),
        }
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            for name, mutation in variants.items():
                with (self.subTest(backend=suffix, variant=name),
                      tempfile.TemporaryDirectory() as tmp):
                    path = Path(tmp)
                    cpp, executable = path / "basis.cpp", path / "basis"
                    cpp.write_text(source(suffix, directory, mutation))
                    build = subprocess.run(
                        [COMPILER, "-std=c++17", "-O2", str(cpp), "-o", str(executable)],
                        capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stderr)
                    run = subprocess.run([str(executable)], capture_output=True, text=True)
                    if name == "production":
                        self.assertEqual(run.returncode, 0, run.stderr)
                        self.assertIn("fallback queries;", run.stdout)
                    else:
                        self.assertNotEqual(run.returncode, 0)
                        expected_basis = "1" if name == "ignore_camera" else "0"
                        self.assertIn("fallback basis=" + expected_basis, run.stderr)


if __name__ == "__main__":
    unittest.main()
