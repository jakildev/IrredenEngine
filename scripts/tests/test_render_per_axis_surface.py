"""Execute continuous per-axis face geometry, decoding and fragment query inputs.

Host adapters preserve shader expressions. Barycentric interpolation is explicit;
these tests do not execute GPU interpolation, coverage, bindings or shadow maps.
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
    functions,
)
from test_render_per_axis_caster_position import (
    harness as caster_harness,
)
from test_render_per_axis_receiver import SPY, host, replace_once

ADAPTER = r"""
vec2 operator*(vec2 a,vec2 b){return {a.x*b.x,a.y*b.y};}
vec2 operator*(vec2 a,float b){return {a.x*b,a.y*b};}
vec2 operator/(vec2 a,float b){return {a.x/b,a.y/b};}
using bvec2=V2<bool>;
bvec2 lessThan(vec2 a,vec2 b){return {a.x<b.x,a.y<b.y};}
bvec2 greaterThan(vec2 a,vec2 b){return {a.x>b.x,a.y>b.y};}
bvec2 operator<(vec2 a,vec2 b){return lessThan(a,b);}
bvec2 operator>(vec2 a,vec2 b){return greaterThan(a,b);}
bool any(bvec2 value){return value.x || value.y;}
vec4 fragmentColor;
struct FaceFields {vec3 faceOrigin;int faceId;};
struct DistanceSample {int r;};
struct DistanceTexture {
    ivec2 cell;
    int encoded;
    DistanceSample read(ivec2 at)const {
        return {at.x==cell.x && at.y==cell.y?encoded:0};
    }
};
DistanceSample texelFetch(DistanceTexture texture,ivec2 at,int){return texture.read(at);}
vec3 cubeSurface(vec3 center,int faceId,vec2 param) {
    const int axis=faceId/2;
    vec3 result=center-vec3(.5f);
    result[axis]+=float(faceId&1);
    result[axis==0?1:0]+=param.x;
    result[axis==2?1:2]+=param.y;
    return result;
}
bool same(vec2 a,vec2 b){return a.x==b.x && a.y==b.y;}
"""

CHECKS = r"""
int main(int argc,char** argv) {
    if(argc>1)mutation=argv[1];
    const ivec2 canvasSize(512,768),perAxisBase(293,370);
    const vec2 points[]={vec2(0,0),vec2(1,0),vec2(0,1),vec2(1,1),
        vec2(.5f,.5f),vec2(.125f,.75f),vec2(.625f,.25f),vec2(-.25f,.5f),vec2(1.25f,.5f)};
    const vec2 selectors[]={vec2(0,0),vec2(1,0),vec2(0,1),vec2(1,1)};
    const vec2 offsets[]={vec2(-.25f,-.125f),vec2(.375f,-.25f),
        vec2(-.125f,.25f),vec2(.25f,.375f)};
    const int triangles[][3]={{0,1,2},{2,1,3}};
    const vec3 weights[]={vec3(1,0,0),vec3(0,1,0),vec3(0,0,1),
        vec3(.25f,.5f,.25f),vec3(.125f,.375f,.5f)};
    const vec4 quaternions[]={vec4(0,0,0,1),vec4(0,0,.6f,.8f),
        vec4(.36f,.48f,0,.8f),vec4(-.5f,.5f,-.5f,.5f)};
    int faces=0,surfacePoints=0,interpolations=0;
    for(int x=0;x<16;++x)for(int y=0;y<16;++y)for(int z=0;z<16;++z)
    for(int faceId=0;faceId<6;++faceId)for(int flip:{0,1}) {
        const int axis=faceId/2,slot=(axis+1)%3;
        const vec3 center((x&1?-16.f:8.f)+x/16.f-.5f,-3+y/16.f-.5f,4+z/16.f-.5f);
        int encoded=0;
        const ivec3 stored=perAxisStoreFacePos({center,1},faceId,slot,axis,flip,encoded);
        const ivec2 cell=perAxisBase+pos3DtoPos2DIso(stored);
        int visibleFaceIds[]={5,0,3};visibleFaceIds[slot]=faceId^flip;
        const FaceFields regular=scatterFace(false,cell,encoded,canvasSize,
                                             perAxisBase,visibleFaceIds);
        const FaceFields overflow=scatterFace(true,cell,encoded,canvasSize,
                                              perAxisBase,visibleFaceIds);
        if(regular.faceId!=faceId || overflow.faceId!=faceId) {
            std::fprintf(stderr,"signed face identity\n");return 1;
        }
        const vec3 expectedOrigin=cubeSurface(center,faceId,vec2(0))+vec3(.5f);
        if(!same(regular.faceOrigin,expectedOrigin))
            return fail("regular origin",regular.faceOrigin,expectedOrigin);
        if(!same(overflow.faceOrigin,expectedOrigin))
            return fail("overflow origin",overflow.faceOrigin,expectedOrigin);
        vec3 normal(0);normal[axis]=(faceId&1)?1.f:-1.f;
        const vec4 quaternion=quaternions[(x+y+z+faceId+flip)%4];
        sunCasterViewToWorld=quaternion;sunFrameData.sunCasterViewToWorld=quaternion;
        for(FaceFields face:{regular,overflow})for(vec2 param:points) {
            const vec3 expected=cubeSurface(center,faceId,param);
            const vec3 actual=perAxisFaceSurfacePoint(face.faceOrigin,face.faceId,param);
            if(!same(actual,expected))return fail("surface point",actual,expected);
            ++surfacePoints;
            calls=0;sampledFrame=nullptr;sampledBuffer=nullptr;
            const float value=fragmentShadow(face,param);
            const bool margin=param.x<0 || param.x>1 || param.y<0 || param.y>1;
            if(margin) {
                if(calls) {std::fprintf(stderr,"margin queried a receiver\n");return 1;}
                if(!same(fragmentColor,vec4(1,1,0,.75f)))return 2;
            } else if(verifySample("fragment",value,expected,normal,quaternion))return 1;
        }
        const vec2 su=(faceId&1)?vec2(2,1):vec2(1,2);
        const vec2 sv=(faceId&1)?vec2(-1,3):vec2(3,-1);
        const vec2 pxPerNdc(2,4);
        vec2 dilated[4];
        for(int vertex=0;vertex<4;++vertex) {
            // Screen displacement is generated from an independent plane-coordinate offset.
            const vec2 displacement=su*offsets[vertex].x+sv*offsets[vertex].y;
            const vec2 dilNdc=displacement/pxPerNdc;
            dilated[vertex]=scatterQuadParam(selectors[vertex],dilNdc,pxPerNdc,su,sv);
        }
        for(const auto& triangle:triangles)for(vec3 weight:weights) {
            const vec2 param=dilated[triangle[0]]*weight.x+
                dilated[triangle[1]]*weight.y+dilated[triangle[2]]*weight.z;
            const vec3 expected=
                cubeSurface(center,faceId,selectors[triangle[0]]+offsets[triangle[0]])*weight.x+
                cubeSurface(center,faceId,selectors[triangle[1]]+offsets[triangle[1]])*weight.y+
                cubeSurface(center,faceId,selectors[triangle[2]]+offsets[triangle[2]])*weight.z;
            const vec3 actual=perAxisFaceSurfacePoint(regular.faceOrigin,faceId,param);
            if(!same(actual,expected))return fail("dilated interpolation",actual,expected);
            ++interpolations;
        }
        ++faces;
    }
    const vec2 degenerate=scatterQuadParam(vec2(1,0),vec2(4,7),vec2(2,4),
                                         vec2(1,2),vec2(2,4));
    if(!same(degenerate,vec2(1,0)))return 2;
    shadowsEnabled=0;sunFrameData.shadowsEnabled=0;calls=0;
    const float disabled=fragmentShadow({vec3(1),0},vec2(.25f,.75f));
    if(calls || disabled!=1.f || !same(fragmentColor,vec4(0,0,0,.75f))) {
        std::fprintf(stderr,"disabled shadow query\n");return 1;
    }
    std::printf("faces=%d surface_points=%d interpolations=%d\n",
                faces,surfacePoints,interpolations);
}
"""


def vertex_decode(vertex):
    vertex = host(vertex).replace("frameData.", "").replace("uint2(", "ivec2(")
    fetch = re.search(r"if \(overflowMode != 0\) \{.*?\n    \} else \{.*?\n    \}",
                      vertex, re.DOTALL)[0]
    fetch = re.sub(r"^\s*color(?:\.a)? = [^;]+;", "", fetch, flags=re.MULTILINE)
    fetch = fetch.replace("gl_InstanceID", "instanceId")
    decode = re.search(r"const int slot = decodeSlot\(rawDist\);.*?"
                       r"const vec3 origin = baseOrigin\b[^;]+;", vertex, re.DOTALL)[0]
    output_source = vertex[vertex.index(decode) + len(decode):]
    outputs = []
    for field in ("faceOrigin", "faceId"):
        varying = "v" + field[0].upper() + field[1:]
        match = re.search(r"(?:" + varying + r"|out\." + field + r") = [^;]+;",
                          output_source)
        if match is None:
            raise ValueError(f"missing vertex output {field}")
        outputs.append(match[0].replace(varying, "out." + field))
    return r"""
FaceFields scatterFace(bool overflowMode,ivec2 cell,int encoded,ivec2 canvasSize,
                       ivec2 perAxisBase,const int* visibleFaceIds) {
    const uint instanceId=1;
    const uint linear=uint(cell.y*canvasSize.x+cell.x);
    const uint packed=uint(cell.x)|(uint(cell.y)<<16u);
    const uint regularCells[]={0u,linear,0u};
    const uint overflowCells[]={0u,0u,0u,packed,0u,uint(encoded)};
    const uint* compactedCells=overflowMode?overflowCells:regularCells;
    const DistanceTexture triangleDistances{cell,encoded};
    ivec2 ij;int rawDist;
""" + fetch + "\n" + decode + "\nFaceFields out;\n" + "\n".join(outputs) \
        + "\nreturn out;\n}\n"


def quad_parameter(vertex):
    source = host(vertex).replace("abs(det)", "std::abs(det)")
    match = re.search(r"const vec2 dilPx = dilNdc \* pxPerNdc;.*?"
                      r"(?:vQuadParam|out\.quadParam) = [^;]+;", source, re.DOTALL)
    if match is None:
        raise ValueError("missing dilated quad parameter calculation")
    body = match[0].replace("out.quadParam", "quadParam").replace("vQuadParam", "quadParam")
    bad = replace_once(body, "cornerSel + dilParam", "cornerSel")
    header = "vec2 scatterQuadParam(vec2 cornerSel,vec2 dilNdc,vec2 pxPerNdc,vec2 su,vec2 sv) {\n"
    return (header.replace("scatterQuadParam", "undilatedQuadParam") + "vec2 quadParam;\n"
            + bad + "\nreturn quadParam;\n}\n" + header
            + 'if(std::strcmp(mutation,"ignore-dilation")==0)'
            + "return undilatedQuadParam(cornerSel,dilNdc,pxPerNdc,su,sv);\nvec2 quadParam;\n"
            + body + "\nreturn quadParam;\n}\n")


def surface_controls(surface):
    name = "perAxisFaceSurfacePoint"
    original = surface.replace(name, name + "Original")
    missing_anchor = replace_once(surface, "- kVoxelRasterCellAnchor", "")
    double_polarity = replace_once(surface, "return faceOrigin",
                                   "return faceOrigin + faceOutwardNormal6(faceId)")
    variants = "\n".join((
        missing_anchor.replace(name, name + "NoAnchor"),
        double_polarity.replace(name, name + "DoublePolarity"),
    ))
    return original + "\n" + variants + r"""
vec3 perAxisFaceSurfacePoint(vec3 faceOrigin,int faceId,vec2 quadParam) {
    if(std::strcmp(mutation,"no-anchor")==0)
        return perAxisFaceSurfacePointNoAnchor(faceOrigin,faceId,quadParam);
    if(std::strcmp(mutation,"double-polarity")==0)
        return perAxisFaceSurfacePointDoublePolarity(faceOrigin,faceId,quadParam);
    return perAxisFaceSurfacePointOriginal(faceOrigin,faceId,quadParam);
}
"""


def fragment_controls(source, suffix):
    source = host(source)
    for shader, adapter in (
        ("in.faceOrigin", "face.faceOrigin"), ("vFaceOrigin", "face.faceOrigin"),
        ("in.faceId", "face.faceId"), ("vFaceId", "face.faceId"),
        ("in.quadParam", "quadParam"), ("vQuadParam", "quadParam"),
        ("in.color.a", "colorAlpha"), ("vColor.a", "colorAlpha"),
        ("out.color", "fragmentColor"), ("FragColor", "fragmentColor"),
    ):
        source = source.replace(shader, adapter)
    margin = re.search(r"const bool inMargin = [^;]+;", source)[0]
    start = source.index("#if IR_PER_AXIS_SURFACE_SHADOW", source.index(margin))
    end = source.index("#else", start)
    body = source[start:end].split("\n", 1)[1]
    query = re.search(r"const float visibility = [^;]+;", body)[0]
    body = replace_once(body, query, query + "\nqueryVisibility = visibility;")
    header = """
float fragmentShadow(FaceFields face,vec2 quadParam) {
    const float colorAlpha=.75f;
    float queryVisibility=-1.f;
"""
    variants = {
        "integer-depth": replace_once(body, "pos3DtoDistance(position)",
                                      "float(int(pos3DtoDistance(position)))"),
        "wrong-normal": replace_once(body, "faceOutwardNormal6(face.faceId)",
                                     "faceOutwardNormal6(face.faceId ^ 1)"),
        "identity-basis": replace_once(body, "sunFrameData.sunCasterViewToWorld"
                                       if suffix == "metal" else "sunCasterViewToWorld",
                                       "vec4(0,0,0,1)"),
        "query-margin": replace_once(body, "if (inMargin)", "if (false)"),
        "query-disabled": replace_once(body, "sunFrameData.shadowsEnabled == 0"
                                       if suffix == "metal" else "shadowsEnabled == 0", "false"),
    }
    generated, select = "", ""
    for index, (label, mutant) in enumerate(variants.items()):
        name = f"fragmentShadowMutation{index}"
        generated += (header.replace("fragmentShadow", name) + margin + "\n" + mutant
                      + "\nreturn queryVisibility;\n}\n")
        select += f'if(std::strcmp(mutation,"{label}")==0)return {name}(face,quadParam);\n'
    return generated + header + select + margin + "\n" + body \
        + "\nreturn queryVisibility;\n}\n"


def harness(suffix, directory):
    root = ROOT / "engine/render/src/shaders" / directory
    common = (root / f"ir_iso_common.{suffix}").read_text()
    vertex_name = "v_peraxis_scatter.glsl" if suffix == "glsl" else "peraxis_scatter.metal"
    vertex = (root / vertex_name).read_text()
    surface = (root / f"ir_per_axis_surface.{suffix}").read_text()
    sampler = (root / f"ir_sun_shadow_sample.{suffix}").read_text()
    fragment = (root / f"ir_peraxis_scatter_fragment_body.{suffix}").read_text()
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
        "faceOutwardNormal6", "decodeSlot", "decodeFlipPerAxis"))
    entry = host(functions(sampler, "worldSurfaceSunShadowFactor"))
    controls = surface_controls(host(functions(surface, "perAxisFaceSurfacePoint")))
    return (f"#define METAL_BACKEND {int(suffix == 'metal')}\n" + geometry + SPY + ADAPTER
            + host(helpers) + "\n" + entry + "\n" + controls + vertex_decode(vertex)
            + quad_parameter(vertex) + fragment_controls(fragment, suffix) + CHECKS)


@unittest.skipUnless(COMPILER, "per-axis surface controls require a C++ compiler")
class PerAxisSurfaceTest(unittest.TestCase):
    def test_surface_geometry_and_continuous_fragment_query(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            with self.subTest(backend=suffix), tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary)
                source, executable = path / "surface.cpp", path / "surface"
                source.write_text(harness(suffix, directory))
                build = subprocess.run(
                    [COMPILER, "-std=c++17", str(source), "-o", str(executable)],
                    capture_output=True, text=True)
                self.assertEqual(build.returncode, 0, build.stderr)
                result = subprocess.run([str(executable)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("faces=49152 surface_points=884736 interpolations=491520",
                              result.stdout)
                for mutant, failure in (
                    ("no-anchor", "surface point"),
                    ("double-polarity", "surface point"),
                    ("ignore-dilation", "dilated interpolation"),
                    ("integer-depth", "fractional world depth"),
                    ("wrong-normal", "receiver polarity"),
                    ("identity-basis", "caster quaternion"),
                    ("query-margin", "margin queried a receiver"),
                    ("query-disabled", "disabled shadow query"),
                ):
                    with self.subTest(mutation=mutant):
                        result = subprocess.run([str(executable), mutant],
                                                capture_output=True, text=True)
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn(failure, result.stderr)


if __name__ == "__main__":
    unittest.main()
