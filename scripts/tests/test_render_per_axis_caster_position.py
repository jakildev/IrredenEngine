"""Execute per-axis caster placement against stored and displayed face geometry.

The CPU adapter preserves shader expressions but does not compile GPU programs,
upload frame flags, or exercise rasterization, atomics, and receiver sampling.
"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMPILER = shutil.which("c++")


def functions(source, name):
    """Keep all overloads, including shader out parameters and integer vectors."""
    pattern = (r"^[ \t]*(?:(?:static|inline|constexpr)\s+)*\w+\s+"
               + re.escape(name) + r"\([^)]*\)\s*\{")
    bodies = []
    for match in re.finditer(pattern, source, re.MULTILINE):
        depth, end = 1, match.end()
        while depth and end < len(source):
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        if depth:
            raise ValueError(f"unclosed function {name}")
        bodies.append(source[match.start():end])
    if not bodies:
        raise ValueError(f"missing function {name}")
    return "\n".join(bodies)


def cpp(source):
    for shader, host in (("float2", "vec2"), ("float3", "vec3"), ("float4", "vec4"),
                         ("int2", "ivec2"), ("int3", "ivec3"), ("bool3", "bvec3")):
        source = re.sub(r"\b" + shader + r"\b", host, source)
    source = re.sub(r"\bout\s+(\w+)\s+(\w+)", r"\1& \2", source)
    return source.replace("thread ", "").replace("faceFrame.", "")


PREAMBLE = r"""
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using std::clamp;
using std::floor;
template<class T> struct V2 {
    T x,y;
    explicit V2(T a=0):x(a),y(a){}
    V2(T a,T b):x(a),y(b){}
    template<class U> explicit V2(V2<U> a):x(T(a.x)),y(T(a.y)){}
    V2 operator+(V2 b)const{return {x+b.x,y+b.y};}
    V2 operator-(V2 b)const{return {x-b.x,y-b.y};}
    V2 operator/(V2 b)const{return {x/b.x,y/b.y};}
};
template<class T> struct V3 {
    T x,y,z;
    explicit V3(T a=0):x(a),y(a),z(a){}
    V3(T a,T b,T c):x(a),y(b),z(c){}
    template<class U> explicit V3(V3<U> a):x(T(a.x)),y(T(a.y)),z(T(a.z)){}
    T& operator[](int i){return i==0?x:(i==1?y:z);}
    T operator[](int i)const{return i==0?x:(i==1?y:z);}
    V3 operator+(V3 b)const{return {x+b.x,y+b.y,z+b.z};}
    V3 operator-(V3 b)const{return {x-b.x,y-b.y,z-b.z};}
    V3 operator*(V3 b)const{return {x*b.x,y*b.y,z*b.z};}
    V3 operator*(T b)const{return {x*b,y*b,z*b};}
    V3 operator/(T b)const{return {x/b,y/b,z/b};}
    V3<bool> operator<=(V3 b)const{return {x<=b.x,y<=b.y,z<=b.z};}
};
using vec2=V2<float>; using ivec2=V2<int>;
using vec3=V3<float>; using ivec3=V3<int>; using bvec3=V3<bool>;
struct vec4 {vec3 xyz; float w;};
vec2 floor(vec2 a){return {std::floor(a.x),std::floor(a.y)};}
vec3 floor(vec3 a){return {std::floor(a.x),std::floor(a.y),std::floor(a.z)};}
vec3 round(vec3 a){return {std::round(a.x),std::round(a.y),std::round(a.z)};}
vec3 abs(vec3 a){return {std::abs(a.x),std::abs(a.y),std::abs(a.z)};}
bvec3 lessThanEqual(vec3 a,vec3 b){return a<=b;}
vec3 mix(vec3 a,vec3 b,vec3 t){return a*(vec3(1)-t)+b*t;}
vec3 select(vec3 a,vec3 b,bvec3 t){return {t.x?b.x:a.x,t.y?b.y:a.y,t.z?b.z:a.z};}
bool same(vec3 a,vec3 b){return a.x==b.x && a.y==b.y && a.z==b.z;}
int fail(const char* label,vec3 actual,vec3 expected){
    std::fprintf(stderr,"%s: actual=(%.9g,%.9g,%.9g) expected=(%.9g,%.9g,%.9g)\n",
        label,actual.x,actual.y,actual.z,expected.x,expected.y,expected.z);
    return 1;
}
"""

CHECKS = r"""
int main(int argc,char** argv) {
    const bool formerQuantizer=argc>1 && std::strcmp(argv[1],"former-quantizer")==0;
    const bool ungrouped=argc>1 && std::strcmp(argv[1],"ungrouped-center")==0;
    const auto cast = formerQuantizer ? formerCasterCenter : casterCenter;
    const auto renderCenter = ungrouped ? ungroupedVoxelCenter : perAxisRenderedVoxelCenter;
    const float buckets[][2] = {
        {0,0},{.0625f,.0625f},{-.0625f,-.0625f},{.1f,.0625f},{-.1f,-.0625f},
        {.30f,.25f},{-.20f,-.1875f},{.25f,.25f},{-.25f,-.25f},
        {.499f,.4375f},{.5f,.5f},{.501f,.5625f},
        {-.499f,-.4375f},{-.5f,-.5f},{-.501f,-.5625f},
        {.00005f,0},{-.00005f,0},{1.00005f,1},{.99995f,1},
        {-1.00005f,-1},{-.99995f,-1},{1.25f,1.25f},{-1.25f,-1.25f}
    };
    // Literal buckets independently pin truncation, half ties and near-grid snapping.
    for(const auto& bucket:buckets) {
        const vec3 actual=renderCenter(vec3(bucket[0]));
        if(!same(actual,vec3(bucket[1])))return fail("bucket",actual,vec3(bucket[1]));
    }
    // These float32 coordinates exceed the packed depth domain; only the helper is exercised.
    for(float position:{1048575.9375f,1048575.875f,1048575.5f,1048576.f,1048576.125f,
                        -1048575.9375f,-1048575.875f,-1048575.5f,-1048576.f,-1048576.125f}) {
        const vec3 actual=renderCenter(vec3(position));
        if(!same(actual,vec3(position)))
            return fail("large-coordinate bucket",actual,vec3(position));
    }
    int faces=0;
    for(int subdivisions:{1,2,4,8,16}) for(int faceId=0;faceId<6;++faceId)
    for(int flip:{0,1}) for(int phase=0;phase<23;++phase) {
        const int axis=faceId/2;
        const vec3 authored(buckets[phase][0]+8,
            buckets[(phase+7)%23][0]-3,buckets[(phase+13)%23][0]+4);
        const vec3 center=cast(authored,subdivisions,0,true);
        int encoded=0;
        const ivec3 facePos=perAxisStoreFacePos({authored,1},faceId,axis,axis,flip,encoded);
        const ivec2 size(512,768);
        const vec2 camera(37.75f,-12.25f);
        const ivec2 base=trixelOriginOffsetZ1(size)+ivec2(floor(camera));
        const ivec2 cell=base+pos3DtoPos2DIso(facePos);
        const vec3 origin=perAxisCellToWorld3DSubCell(
            cell,encoded,faceId,size,camera,ivec2(1,subdivisions));
        const vec3 scatter=storedScatterOrigin(cell-base,encoded,axis);
        if(!same(origin,scatter))return fail("scatter decode",origin,scatter);
        const vec3 corner=casterCorner(center,faceId);
        vec3 expectedCorner=center-vec3(.5f);
        expectedCorner[axis]+=float(faceId&1);
        if(!same(corner,expectedCorner))return fail("center anchor",corner,expectedCorner);
        for(float u:{0.f,.5f,1.f}) for(float v:{0.f,.5f,1.f}) {
            vec3 expected=corner;
            // Independent cube coordinates; the signed axis is already in corner.
            expected[axis==0?1:0]+=u;
            expected[axis==2?1:2]+=v;
            const vec3 displayed=faceSpanCorner(axis,scatter,vec2(u,v))-kVoxelRasterCellAnchor;
            if(!same(displayed,expected))return fail("caster/display face",expected,displayed);
        }
        ++faces;
    }
    // Full frac-field sweep covers every permutation of the face-local basis.
    for(int x=0;x<16;++x) for(int y=0;y<16;++y) for(int z=0;z<16;++z) {
        const vec3 authored(8+x/16.f-.5f,-3+y/16.f-.5f,4+z/16.f-.5f);
        if(!same(perAxisRenderedVoxelCenter(authored),authored))return 2;
        for(int face=0;face<6;++face) {
            int encoded;
            const ivec3 pos=perAxisStoreFacePos({authored,1},face,face/2,face/2,0,encoded);
            const vec3 origin=storedScatterOrigin(pos3DtoPos2DIso(pos),encoded,face/2);
            const vec3 corner=casterCorner(cast(authored,1,0,true),face);
            const vec3 displayed=faceSpanCorner(face/2,origin,vec2(0))-kVoxelRasterCellAnchor;
            if(!same(corner,displayed))return fail("frac triple",corner,displayed);
        }
    }
    for(int subdivisions:{1,2,4,8,16}) for(const auto& bucket:buckets) {
        const vec3 authored(bucket[0],bucket[0]+2,bucket[0]-4);
        for(bool perAxis:{false,true}) {
            const vec3 rigid=cast(authored,subdivisions,2,perAxis);
            if(!same(rigid,authored))return fail("rigid",rigid,authored);
        }
        // Cardinal and detached-resampled routes retain their subdivision quantizer.
        vec3 expected;
        for(int axis=0;axis<3;++axis) {
            float value=authored[axis];
            const float nearest=std::round(value);
            if(std::abs(value-nearest)<=.0001f)value=nearest;
            expected[axis]=float(std::floor(value*subdivisions+.5f))/subdivisions;
        }
        for(int basis:{0,1}) {
            const vec3 actual=cast(authored,subdivisions,basis,false);
            if(!same(actual,expected))return fail("non-per-axis",actual,expected);
        }
    }
    for(int subdivisions:{1,2,4,8,16}) for(int x=-8;x<=8;++x) {
        const vec3 authored(float(x),float(3*x),float(-2*x));
        if(!same(cast(authored,subdivisions,0,true),authored))return 3;
        if(!same(cast(authored,subdivisions,0,false),authored))return 4;
    }
    std::printf("faces=%d frac_triples=4096 route_controls=passed\n",faces);
}
"""


def harness(suffix, directory):
    root = ROOT / "engine/render/src/shaders" / directory
    common = (root / f"ir_iso_common.{suffix}").read_text()
    lighting = (root / f"ir_per_axis_lighting.{suffix}").read_text()
    store = (root / f"ir_voxel_face_select.{suffix}").read_text()
    scatter_file = "v_peraxis_scatter.glsl" if suffix == "glsl" else "peraxis_scatter.metal"
    scatter = (root / scatter_file).read_text()
    caster = cpp((root / f"c_bake_voxel_sun_faces.{suffix}").read_text())
    constants = "\n".join(re.findall(
        r"^(?:const|constant) (?:int|vec3|float3) (?:kFace\w+|kVoxelRasterCellAnchor) = [^;]+;",
        common, re.MULTILINE)).replace("constant ", "const ")
    names = (
        "fracToFrac4", "snapNearIntegerVoxelPosition", "roundHalfUp",
        "perAxisRenderedVoxelCenter", "pos3DtoDistance", "pos3DtoPos2DIso",
        "isoPositionToPos3D", "isoPixelToPos3D", "faceMicroPositionFixed6",
        "encodeDepthWithFaceFrac", "decodeDepthPerAxis", "decodeUFrac4PerAxis",
        "decodeVFrac4PerAxis", "decodeWFrac4PerAxis", "faceInPlaneUnitAxes",
        "faceOutOfPlaneUnitAxis", "trixelOriginOffsetX1", "trixelOriginOffsetZ1",
    )
    helpers = constants + "\n" + "\n".join(functions(common, name) for name in names)
    helpers += "\n" + functions(store, "perAxisStoreFacePos")
    helpers += "\n" + "\n".join(functions(lighting, name) for name in (
        "perAxisCellToWorld3D", "perAxisSubCellFrac", "perAxisCellToWorld3DSubCell"))
    helpers += "\n" + functions(scatter, "faceSpanCorner")
    center = cpp(functions(common, "perAxisRenderedVoxelCenter"))
    ungrouped, replacements = re.subn(
        r"\+ \((vec3\(u, v, w\) / 16\.0 - vec3\(0\.5\))\)", r"+ \1", center)
    if replacements != 1:
        raise ValueError("grouping mutation did not remove exactly one fraction grouping")
    helpers += "\n" + ungrouped.replace("perAxisRenderedVoxelCenter", "ungroupedVoxelCenter")
    scatter_origin = re.search(
        r"const (?:vec3|float3) origin = baseOrigin\b[^;]+;", scatter)[0]
    decode = """
vec3 storedScatterOrigin(ivec2 iso,int encoded,int axis) {
    const vec3 baseOrigin=isoPixelToPos3D(iso.x,iso.y,float(decodeDepthPerAxis(encoded)));
    const int uFrac4=decodeUFrac4PerAxis(encoded),vFrac4=decodeVFrac4PerAxis(encoded);
    const int wFrac4=decodeWFrac4PerAxis(encoded);
    vec3 eu,ev;faceInPlaneUnitAxes(axis,eu,ev);
""" + cpp(scatter_origin) + "\nreturn origin;\n}\n"
    select = re.search(r"const bool rigidSource = [^;]+;\s*const vec3 position = [^;]+;",
                       caster)[0]
    legacy = ("vec3(roundHalfUp(snapNearIntegerVoxelPosition(positions[index].xyz)"
              " * subdivisions)) / subdivisions")
    previous = select.replace("perAxisRenderedVoxelCenter(positions[index].xyz)", legacy)
    if previous == select:
        raise ValueError("caster mutation did not replace the per-axis center")
    wrappers = ""
    for name, expression in (("casterCenter", select), ("formerCasterCenter", previous)):
        wrappers += "\nvec3 " + name + """(vec3 authored,int density,int basis,bool perAxis) {
    struct Dispatch {int w;} dispatch{basis};
    const vec4 worldOrigin{vec3(0),perAxis?1.f:0.f};
    const vec4 positions[]={{authored,1}};
    const int index=0;
    const float subdivisions=float(density);
""" + expression + "\nreturn position;\n}\n"
    corner = re.search(r"vec3 corner = [^;]+;\s*corner\[axis\] \+= [^;]+;", caster)[0]
    wrappers += """
vec3 casterCorner(vec3 position,int faceId) {
    const int axis=faceId/2;
    const bool positive=(faceId&1)!=0;
""" + corner + "\nreturn corner;\n}\n"
    return PREAMBLE + cpp(helpers) + decode + wrappers + CHECKS


@unittest.skipUnless(COMPILER, "per-axis geometry controls require a C++ compiler")
class PerAxisCasterPositionTest(unittest.TestCase):
    def test_caster_matches_display_and_rejects_subdivision_quantizer(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            with self.subTest(backend=suffix), tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary)
                source, executable = path / "positions.cpp", path / "positions"
                source.write_text(harness(suffix, directory))
                build = subprocess.run(
                    [COMPILER, "-std=c++17", str(source), "-o", str(executable)],
                    capture_output=True, text=True)
                self.assertEqual(build.returncode, 0, build.stderr)
                result = subprocess.run([str(executable)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("route_controls=passed", result.stdout)
                mutation = subprocess.run([str(executable), "former-quantizer"],
                                          capture_output=True, text=True)
                self.assertNotEqual(mutation.returncode, 0)
                self.assertIn("caster/display face", mutation.stderr)
                mutation = subprocess.run([str(executable), "ungrouped-center"],
                                          capture_output=True, text=True)
                self.assertNotEqual(mutation.returncode, 0)
                self.assertIn("large-coordinate bucket", mutation.stderr)


if __name__ == "__main__":
    unittest.main()
