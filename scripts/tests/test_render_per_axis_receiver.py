"""Execute per-axis receiver geometry and shadow call sites from both backends.

The host adapter preserves shader expressions and spies on the sampler entry.
It does not compile GPU kernels, upload frame data, or validate shadow-map pixels.
"""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_render_per_axis_caster_position import (
    CHECKS as CASTER_CHECKS,
)
from test_render_per_axis_caster_position import (
    COMPILER,
    ROOT,
    cpp,
    functions,
)
from test_render_per_axis_caster_position import (
    harness as caster_harness,
)


def replace_once(source, before, after):
    if source.count(before) != 1:
        raise ValueError(f"expected one mutation site for {before!r}")
    return source.replace(before, after)


def host(source):
    return cpp(source).replace("constant ", "const ").replace("device ", "")


SPY = r"""
using uint=unsigned int;
vec3 operator*(double scale,vec3 value){return value*float(scale);}
vec3 operator-(vec3 value){return value*-1.f;}
bool same(vec4 a,vec4 b){return same(a.xyz,b.xyz) && a.w==b.w;}
struct FrameDataSun {int shadowsEnabled;vec4 sunCasterViewToWorld;};
const char* mutation="";
int calls=0;
vec3 sampledPosition,sampledNormal;
float sampledDepth;
bool sampledSurface;
vec4 sampledQuaternion;
const FrameDataSun* sampledFrame;
const uint* sampledBuffer;
const uint sunDepthBuf[]={123u};
FrameDataSun sunFrameData{1,{0,0,0,1}};
int shadowsEnabled=1;
vec4 sunCasterViewToWorld(0,0,0,1);
float worldSunShadowFactorImpl(vec3 position,vec3 normal,float depth,
                              bool surface,vec4 quaternion) {
    ++calls;sampledPosition=position;sampledNormal=normal;sampledDepth=depth;
    sampledSurface=surface;sampledQuaternion=quaternion;
    return .3125f;
}
float worldSunShadowFactorImpl(vec3 position,vec3 normal,float depth,
                              const FrameDataSun& frame,const uint* buffer,
                              bool surface,vec4 quaternion) {
    sampledFrame=&frame;sampledBuffer=buffer;
    return worldSunShadowFactorImpl(position,normal,depth,surface,quaternion);
}
int verifySample(const char* route,float result,vec3 expected,vec3 normal,vec4 quaternion) {
    if(calls!=1 || result!=.3125f || !sampledSurface) {
        std::fprintf(stderr,"%s: finite sampler route/call count/result\n",route);return 1;
    }
    if(!same(sampledPosition,expected))return fail("receiver center",sampledPosition,expected);
    if(!same(sampledNormal,normal))return fail("receiver polarity",sampledNormal,normal);
    if(sampledDepth!=expected.x+expected.y+expected.z) {
        std::fprintf(stderr,"%s: fractional world depth %.9g expected %.9g\n",
            route,sampledDepth,expected.x+expected.y+expected.z);return 1;
    }
    if(!same(sampledQuaternion,quaternion)) {
        std::fprintf(stderr,"%s: caster quaternion\n",route);return 1;
    }
#if METAL_BACKEND
    if(sampledFrame!=&sunFrameData || sampledBuffer!=sunDepthBuf) {
        std::fprintf(stderr,"%s: Metal frame/buffer forwarding\n",route);return 1;
    }
#endif
    return 0;
}
"""


CHECKS = r"""
int main(int argc,char** argv) {
    if(argc>1)mutation=argv[1];
    if(checkCameraRefresh())return 1;
    const vec4 quaternions[]={
        {0,0,0,1},{0,0,.6f,.8f},{.36f,.48f,0,.8f},{-.5f,.5f,-.5f,.5f}
    };
    int faces=0,fractionalDepths=0,rawDepthDifferences=0;
    const ivec2 size(512,768);
    const vec2 camera(37.75f,-12.25f);
    const ivec2 base=trixelOriginOffsetZ1(size)+ivec2(floor(camera));
    for(int x=0;x<16;++x)for(int y=0;y<16;++y)for(int z=0;z<16;++z)
    for(int faceId=0;faceId<6;++faceId)for(int flip:{0,1}) {
        const int axis=faceId/2,slot=(axis+1)%3;
        const float translation=(x&1)?-16.f:8.f;
        const vec3 center(translation+x/16.f-.5f,-3+y/16.f-.5f,4+z/16.f-.5f);
        vec3 normal(0);normal[axis]=(faceId&1)?1.f:-1.f;
        const vec3 expected=center+normal*.5f;
        int encoded=0;
        const ivec3 stored=perAxisStoreFacePos({center,1},faceId,slot,axis,flip,encoded);
        const ivec2 cell=base+pos3DtoPos2DIso(stored);
        const ivec2 options(1,1<<((x+y+z)%5));
        const vec3 origin=perAxisCellToWorld3DSubCell(cell,encoded,faceId,size,camera,options);
        const vec3 scatter=storedScatterOrigin(cell-base,encoded,axis);
        const vec3 displayed=faceSpanCorner(axis,scatter,vec2(.5f))-kVoxelRasterCellAnchor;
        if(!same(displayed,expected))
            return fail("explicit cube/scatter midpoint",displayed,expected);
        const vec3 actual=perAxisFaceCenter(origin,faceId);
        if(!same(actual,expected))return fail("decoded face center",actual,expected);
        const vec4 quaternion=quaternions[(x+y+z+faceId+flip)%4];
        sunCasterViewToWorld=quaternion;sunFrameData.sunCasterViewToWorld=quaternion;
        int visibleFaceIds[]={5,0,3};visibleFaceIds[slot]=faceId^flip;
        for(int route=0;route<3;++route) {
            calls=0;sampledFrame=nullptr;sampledBuffer=nullptr;
            float result;
            if(route==0)result=directReceiver(origin,faceId);
            else if(route==1)
                result=regularReceiver(cell,encoded,visibleFaceIds,size,camera,options);
            else result=overflowReceiver(cell,encoded,visibleFaceIds,size,camera,options);
            if(verifySample("per-axis",result,expected,normal,quaternion))return 1;
        }
        const float depth=expected.x+expected.y+expected.z;
        fractionalDepths+=depth!=float(int(depth));
        rawDepthDifferences+=depth!=float(decodeDepthPerAxis(encoded));
        ++faces;
    }
    // Disabled shadows must avoid querying either regular or overflow receivers.
    shadowsEnabled=0;sunFrameData.shadowsEnabled=0;
    for(int flip:{0,1}) {
        int encoded=0;
        const ivec3 stored=perAxisStoreFacePos({vec3(0),1},1,0,0,flip,encoded);
        int visibleFaceIds[]={1^flip,2,4};
        const ivec2 cell=base+pos3DtoPos2DIso(stored);
        calls=0;
        if(regularReceiver(cell,encoded,visibleFaceIds,size,camera,ivec2(1))!=1.f || calls)
            return 2;
        if(overflowReceiver(cell,encoded,visibleFaceIds,size,camera,ivec2(1))!=1.f || calls)
            return 3;
    }
    if(!fractionalDepths || !rawDepthDifferences)return 4;
    calls=0;
    const int visibleFaceIds[]={0,2,4};
    if(regularReceiver(base,0,visibleFaceIds,size,camera,ivec2(1),0)!=-1.f || calls)return 5;
    std::printf("faces=%d receiver_calls=%d fractional_depths=%d raw_depth_differences=%d\n",
        faces,faces*3,fractionalDepths,rawDepthDifferences);
}
"""


def statement(source, name):
    match = re.search(r"\b(?:const\s+)?\w+\s+" + name + r"\s*=\s*[^;]+;", source)
    if match is None:
        raise ValueError(f"missing statement {name}")
    return match[0]


def call_sites(root, suffix):
    regular = host((root / f"c_compute_sun_shadow_body.{suffix}").read_text())
    overflow = host((root / f"c_light_overflow_faces.{suffix}").read_text())
    regular = regular.replace("frameData.", "")
    overflow = overflow.replace("voxelFrameData.", "")
    branch = re.search(r"if \(perAxisRoute != 0\) \{\s*const int faceId\b.*?\n    \}",
                       regular, re.DOTALL)
    if branch is None:
        raise ValueError("missing regular per-axis shadow branch")
    body = branch[0]
    sink = r"(?:imageStore\(canvasSunShadow,|canvasSunShadow\.write\()[^;]+;\s*return;"
    body, count = re.subn(sink, "return factor;", body)
    if count != 1:
        raise ValueError("regular shadow branch must store once and return")
    decode = "\n".join(statement(regular, name) for name in ("face", "flip"))
    regular_header = """
float regularReceiver(ivec2 pixel,int encoded,const int* visibleFaceIds,
                      ivec2 size,vec2 frameCanvasOffset,ivec2 voxelRenderOptions,
                      int perAxisRoute=1) {
"""
    regular_body = decode + "\n" + body + "\nreturn -1.f;\n}\n"
    overflow_header = """
float overflowReceiver(ivec2 inputCell,int rawDist,const int* visibleFaceIds,
                       ivec2 canvasSizePixels,vec2 frameCanvasOffset,ivec2 voxelRenderOptions) {
    const uint packedCell=uint(inputCell.x)|(uint(inputCell.y)<<16u);
"""
    overflow_body = "\n".join(statement(overflow, name) for name in (
        "cell", "slot", "flip", "faceId", "worldNormal", "pos3D", "shadow"))
    overflow_body += "\nreturn shadow;\n}\n"
    result = ""
    for name, header, body in (("regular", regular_header, regular_body),
                               ("overflow", overflow_header, overflow_body)):
        bad = replace_once(body, " ^ flip", "")
        header_bad = header.replace(name + "Receiver", name + "UnflippedReceiver")
        result += header_bad + bad
        args = ("pixel,encoded,visibleFaceIds,size,frameCanvasOffset,"
                "voxelRenderOptions,perAxisRoute"
                if name == "regular" else
                "inputCell,rawDist,visibleFaceIds,canvasSizePixels,frameCanvasOffset,voxelRenderOptions")
        select = (f'if(std::strcmp(mutation,"{name}-flip")==0)'
                  f"return {name}UnflippedReceiver({args});\n")
        result += header + select + body
    return result


def wrapper_controls(wrapper, center, surface, suffix):
    mutants = {
        "no-center": replace_once(wrapper, "perAxisFaceCenter(faceOrigin, faceId)", "faceOrigin"),
        "signed-axis": replace_once(wrapper, "perAxisFaceCenter(", "signedAxisCenter("),
        "legacy-offset": replace_once(wrapper, "worldSurfaceSunShadowFactor(", "biasedSurface("),
        "integer-depth": replace_once(wrapper, "pos3DtoDistance(center)",
                                      "float(int(pos3DtoDistance(center)))"),
        "normal-polarity": replace_once(wrapper, "faceOutwardNormal6(faceId)",
                                        "faceOutwardNormal6(faceId ^ 1)"),
        "identity-quaternion": replace_once(wrapper,
                                            "sun.sunCasterViewToWorld" if suffix == "metal"
                                            else "sunCasterViewToWorld", "vec4(0,0,0,1)"),
    }
    legacy, count = re.subn(r",\s*(?:sun\.)?sunCasterViewToWorld\b", "", wrapper)
    if count != 1:
        raise ValueError("missing legacy-wrapper mutation quaternion")
    mutants["legacy-route"] = replace_once(legacy, "worldSurfaceSunShadowFactor(",
                                            "worldSunShadowFactor(")
    signed = replace_once(center, "faceOutOfPlaneUnitAxis(faceId >> 1)",
                          "faceOutwardNormal6(faceId)")
    signed = signed.replace("perAxisFaceCenter", "signedAxisCenter")
    biased = replace_once(surface, "worldSunShadowFactorImpl(pos3D,",
                          "worldSunShadowFactorImpl(pos3D + normal * kNormalBiasVoxels,")
    biased = biased.replace("worldSurfaceSunShadowFactor", "biasedSurface")
    name = "perAxisSunShadowFactor"
    generated = signed + "\n" + biased + "\n" + wrapper.replace(name, name + "Original")
    selection = ""
    args = "faceOrigin,faceId" + (",sun,sunDepthBuf" if suffix == "metal" else "")
    for index, (label, mutant) in enumerate(mutants.items()):
        renamed = f"{name}Mutation{index}"
        generated += "\n" + mutant.replace(name, renamed)
        selection += f'if(std::strcmp(mutation,"{label}")==0)return {renamed}({args});\n'
    return generated + "\n" + wrapper[:wrapper.index("{") + 1] + selection \
        + f"return {name}Original({args});\n}}\n"


def camera_refresh():
    source = (ROOT / "engine/prefabs/irreden/render/systems/"
              "system_bake_sun_shadow_map.hpp").read_text()
    update = functions(source, "updateSunFrameData")
    prefix = update[:update.index("vec3 uHat, vHat;")] + "}\n"
    assignment = re.search(r"frameData_\.sunCasterViewToWorld_ = [^;]+;", prefix)[0]
    late = replace_once(prefix, assignment, "")
    late = late[:late.rindex("}")] + assignment + "\n}\n"
    late = late.replace("updateSunFrameData", "updateCameraAfterGuard")
    return r"""
vec3& operator/=(vec3& value,float scale){value=value/scale;return value;}
namespace IRMath {float length(vec3 v){return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);}}
namespace detail {
struct ResolvedSun {
    vec3 direction_;float intensity_,ambient_;bool shadowsEnabled_,aoEnabled_;
};
ResolvedSun inputSun{vec3(1,2,2),.9f,.1f,true,true};
ResolvedSun resolveSun(){return inputSun;}
}
namespace IRPrefab::Camera {
vec4 inputCamera(0,0,0,1);
int reads=0;
vec4 getRotationQuat(){++reads;return inputCamera;}
}
struct UploadPrefix {
    struct Data {
        vec4 sunDirection_,sunCasterViewToWorld_;
        float sunIntensity_,sunAmbient_;
        int shadowsEnabled_,aoEnabled_;
    } frameData_;
""" + prefix + late + r"""
};
int checkCameraRefresh() {
    UploadPrefix owner;
    for(bool enabled:{true,false,true,false})for(vec4 quaternion:{
        vec4(0,0,0,1),vec4(.36f,.48f,0,.8f),vec4(-.5f,.5f,-.5f,.5f)}) {
        detail::inputSun.shadowsEnabled_=enabled;
        IRPrefab::Camera::inputCamera=quaternion;IRPrefab::Camera::reads=0;
        owner.frameData_.sunCasterViewToWorld_=vec4(1,0,0,0);
        if(std::strcmp(mutation,"camera-after-guard")==0)owner.updateCameraAfterGuard();
        else owner.updateSunFrameData();
        if(IRPrefab::Camera::reads!=1 ||
           !same(owner.frameData_.sunCasterViewToWorld_,quaternion)) {
            std::fprintf(stderr,"camera refresh before shadows-disabled return\n");return 1;
        }
    }
    return 0;
}
"""


def harness(suffix, directory):
    root = ROOT / "engine/render/src/shaders" / directory
    common = (root / f"ir_iso_common.{suffix}").read_text()
    lighting = (root / f"ir_per_axis_lighting.{suffix}").read_text()
    shadow = (root / f"ir_per_axis_shadow.{suffix}").read_text()
    sampler = (root / f"ir_sun_shadow_sample.{suffix}").read_text()
    geometry = caster_harness(suffix, directory).removesuffix(CASTER_CHECKS)
    geometry = replace_once(geometry, "struct vec4 {vec3 xyz; float w;};", """
struct vec4 {
    vec3 xyz;float w;
    vec4():xyz(0),w(0){}
    vec4(vec3 value,float last):xyz(value),w(last){}
    vec4(float x,float y,float z,float last):xyz(x,y,z),w(last){}
};
""")
    helpers = "\n".join(functions(common, name) for name in (
        "faceOutwardNormal6", "decodeSlot", "decodeFlipPerAxis", "decodeFlipSingle",
        "decodeFlipRoute"))
    center = host(functions(lighting, "perAxisFaceCenter"))
    helpers += "\n" + center
    bias = re.search(r"(?:const|constant) float kNormalBiasVoxels = [^;]+;", sampler)[0]
    entries = "\n".join(functions(sampler, name) for name in (
        "worldSunShadowFactor", "worldSurfaceSunShadowFactor"))
    wrapper = host(functions(shadow, "perAxisSunShadowFactor"))
    controls = wrapper_controls(wrapper, center, host(functions(
        sampler, "worldSurfaceSunShadowFactor")), suffix)
    extra = ", sunFrameData, sunDepthBuf" if suffix == "metal" else ""
    direct = "float directReceiver(vec3 origin,int faceId) {return perAxisSunShadowFactor(" \
             + "origin,faceId" + extra + ");}\n"
    return (f"#define METAL_BACKEND {int(suffix == 'metal')}\n" + geometry + SPY
            + host(helpers) + "\n" + host(bias) + "\n" + host(entries) + "\n"
            + controls + "\n" + direct + call_sites(root, suffix) + camera_refresh() + CHECKS)


@unittest.skipUnless(COMPILER, "per-axis receiver controls require a C++ compiler")
class PerAxisReceiverTest(unittest.TestCase):
    def test_displayed_face_center_and_shadow_contract(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            with self.subTest(backend=suffix), tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary)
                source, executable = path / "receiver.cpp", path / "receiver"
                source.write_text(harness(suffix, directory))
                build = subprocess.run(
                    [COMPILER, "-std=c++17", str(source), "-o", str(executable)],
                    capture_output=True, text=True)
                self.assertEqual(build.returncode, 0, build.stderr)
                result = subprocess.run([str(executable)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("faces=49152 receiver_calls=147456", result.stdout)
                for mutant, failure in (
                    ("no-center", "receiver center"),
                    ("signed-axis", "receiver center"),
                    ("legacy-offset", "receiver center"),
                    ("legacy-route", "finite sampler route"),
                    ("integer-depth", "fractional world depth"),
                    ("normal-polarity", "receiver polarity"),
                    ("identity-quaternion", "caster quaternion"),
                    ("regular-flip", "receiver polarity"),
                    ("overflow-flip", "receiver polarity"),
                    ("camera-after-guard", "camera refresh before shadows-disabled return"),
                ):
                    with self.subTest(mutation=mutant):
                        result = subprocess.run([str(executable), mutant],
                                                capture_output=True, text=True)
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn(failure, result.stderr)


if __name__ == "__main__":
    unittest.main()
