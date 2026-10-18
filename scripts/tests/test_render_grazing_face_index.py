"""Execute trig-cardinal box-face queries and measure actual index insertions.

CPU adapters suffix shader floating literals to retain binary32 arithmetic;
independent geometry oracles use exact binary64 sums. These are host tests,
not native GPU fast-math validation.
"""

import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_function
from test_render_source_face_index import COMPILER, PREAMBLE, ROOT, load_shader, shader_to_cpp

FIXTURE = r"""
float dot(vec3 a,vec3 b){return (a.x*b.x+a.y*b.y)+a.z*b.z;}
vec3 operator+(vec3 a,vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
vec3 operator*(vec3 a,float b){return {a.x*b,a.y*b,a.z*b};}
struct Basis {vec3 sun,u,v;};
Basis basis(float azimuthDegrees,float elevationDegrees){
    const float azimuth=azimuthDegrees*0.017453292519943295f;
    const float elevation=elevationDegrees*0.017453292519943295f;
    const float horizontal=std::cos(elevation);
    vec3 n{horizontal*std::cos(azimuth),horizontal*std::sin(azimuth),-std::sin(elevation)};
    const float length=std::sqrt(dot(n,n));
    n={n.x/length,n.y/length,n.z/length};
    const float s=std::copysign(1.0f,n.z),a=-1.0f/(s+n.z),b=(n.x*n.y)*a;
    return {n,{1.0f+((s*n.x)*n.x)*a,s*b,-s*n.x},{b,s+(n.y*n.y)*a,-n.y}};
}
PROJECTION
struct Face {vec3 corner,u,v;};
vec3 project(vec3 point,Basis frame){
    const vec3 actual=sunSpaceProject(point,frame.u,frame.v,frame.sun);
    const vec3 axes[]={frame.u,frame.v,frame.sun};
    for(uint axis=0;axis<3;++axis){
        const vec3 a=axes[axis];
        const double expected=(double(point.x)*a.x+double(point.y)*a.y)+double(point.z)*a.z;
        const double magnitude=std::abs(double(point.x)*a.x)+std::abs(double(point.y)*a.y)+
                               std::abs(double(point.z)*a.z);
        if(std::abs(double(actual[axis])-(axis==2?-expected:expected))>magnitude*1e-6+1e-12){
            std::cerr<<"projection differs from independent dot-product oracle\n";std::exit(1);
        }
    }
    return actual;
}
Face boxFace(Basis frame,float edge,int axis,vec3 center){
    float c[3]={-edge*.5f,-edge*.5f,-edge*.5f},u[3]={},v[3]={};
    c[axis]=frame.sun[axis]>0?edge*.5f:-edge*.5f;
    u[(axis+1)%3]=edge;v[(axis+2)%3]=edge;
    return {project(center+vec3{c[0],c[1],c[2]},frame),
            project({u[0],u[1],u[2]},frame),project({v[0],v[1],v[2]},frame)};
}
std::vector<uint> storage(kSourceFaceBufferWords,0xccccccccu);
void reset(){
    sunDepthBuf={storage.data(),kSourceFaceBufferWords};
    sunDepthBuf[kSourceFaceHeaderOffset]=sunDepthBuf[kSourceFaceSpillCounterOffset]=0;
    atomicAdds=atomicMaxes=atomicExchanges=0;
    for(uint tile=0;tile<kSourceFaceTileCount;++tile){
        sunDepthBuf[sourceFaceTileBase(tile)]=0;
        sunDepthBuf[kSourceFaceSpillHeadOffset+tile]=0xffffffffu;
    }
}
void emit(Face face){const vec3 corner=face.corner,u=face.u,v=face.v;CALL_INDEX}
void require(bool value,const char* message){
    if(!value){std::cerr<<message<<"\n";std::exit(1);}
}
void envelopeGeometry(){
    uint points=0;
    // Dyadic coefficients and similarly scaled binary32 inputs keep every
    // reference sum exact in binary64, including severe cancellation.
    for(int exponent:{-120,-60,-1,0,20,55})
    for(float c:{-3.f,-1.f,0.f,1.f,3.f})
    for(float u:{-1.00000011920928955078125f,-1.f,1.f,1.00000011920928955078125f})
    for(float v:{-1.0000002384185791015625f,-1.f,1.f,1.0000002384185791015625f}){
        const float corner=std::ldexp(c,exponent),edgeU=std::ldexp(u,exponent);
        const float edgeV=std::ldexp(v,exponent);
        for(double a:{0.,0x1p-20,.25,.5,.75,1.-0x1p-20,1.})
        for(double b:{0.,0x1p-20,.25,.5,.75,1.-0x1p-20,1.}){
            const double exact=double(corner)+a*double(edgeU)+b*double(edgeV);
            require(sourceFaceWithinProjectedBounds(float(exact),corner,edgeU,edgeV),
                    "binary64 closed geometry must remain inside receiver envelope");
            ++points;
        }
    }
    require(points==23520,"independent envelope fixture count");
    std::cout<<"binary64_envelope_points="<<points<<"\n";
}
uint checkMembership(vec2 sunUV){
    uint checked=0;
    for(uint cascade=0;cascade<2;++cascade){
        const vec2 origin=cascade==0?cascadeOriginUV_0:cascadeOriginUV_1;
        const vec2 texelSz=cascade==0?cascadeTexelSize_0:cascadeTexelSize_1;
        QUERY_TILE
        if(tile.x<0||tile.y<0||tile.x>=128||tile.y>=128)continue;
        const uint base=sourceFaceTileBase(cascade*128*128+uint(tile.y)*128+uint(tile.x));
        require(sunDepthBuf[base]==1&&sunDepthBuf[base+1]==0,
                "accepted represented face point must remain indexed");
        ++checked;
    }
    return checked;
}
void workload(bool check){
    for(float azimuth:{90.f,180.f})for(float edge:{2.f,4.f,5.f,10.f}){
        reset();const Basis frame=basis(azimuth,30);
        const int axis=azimuth==90?0:1;
        const Face first=boxFace(frame,edge,axis,{0,0,0});
        for(uint row=0;row<8;++row)for(uint column=0;column<16;++column)
            emit(boxFace(frame,edge,axis,frame.u*float(64+column*12)+frame.v*float(64+row*12)));
        const uint records=sunDepthBuf[kSourceFaceHeaderOffset];
        const uint spill=sunDepthBuf[kSourceFaceSpillCounterOffset];
        const auto inserts=atomicAdds-records-spill;
        uint incomplete=0,occupied=0;
        for(uint tile=0;tile<kSourceFaceTileCount;++tile){
            const uint count=sunDepthBuf[sourceFaceTileBase(tile)];
            incomplete+=!sourceFaceQueryComplete(count);occupied+=count!=0;
        }
        std::cout<<"azimuth="<<azimuth<<" edge="<<edge
                 <<" determinant="<<projectedFaceDeterminant(first.u.xy(),first.v.xy())
                 <<" records="<<records<<" tile_inserts="<<inserts
                 <<" spill_reservations="<<spill<<" incomplete="<<incomplete
                 <<" occupied="<<occupied<<"\n";
        if(check){
            require(inserts<128*64,"grazing grid must have a finite index workload");
            require(incomplete==0&&spill==0,"grazing grid must keep complete inline lists");
            if(edge==10)require(records==128,"nonzero thin box faces must remain indexed");
        }
    }
}
void geometry(){
    uint points=0,thinPoints=0,farRejected=0,negativeDeterminants=0,positiveDeterminants=0;
    uint memberships=0;
    for(float azimuth:{33.f,90.f,180.f,270.f})for(float edge:{4.f,5.f,10.f}){
        const Basis frame=basis(azimuth,30);
        for(int axis=0;axis<3;++axis)for(int winding=0;winding<8;++winding)
        for(vec3 center:{vec3(0,0,0),vec3(11,-17,5),vec3(100,64,-32)}){
            Face f=boxFace(frame,edge,axis,center);
            if(winding&1)std::swap(f.u,f.v);
            if(winding&2){f.corner=f.corner+f.u;f.u=f.u*-1.f;}
            if(winding&4){f.corner=f.corner+f.v;f.v=f.v*-1.f;}
            const float determinant=projectedFaceDeterminant(f.u.xy(),f.v.xy());
            if(std::abs(determinant)<1e-6f)continue;
            reset();emit(f);
            negativeDeterminants+=determinant<0;positiveDeterminants+=determinant>0;
            const float sunZ=f.corner.z+std::abs(f.u.z)+std::abs(f.v.z)+16.f;
            for(double a:{0.0,1e-6,.25,.5,.75,1.0-1e-6,1.0})
            for(double b:{0.0,1e-6,.25,.5,.75,1.0-1e-6,1.0}){
                const vec2 point{float(double(f.corner.x)+a*double(f.u.x)+b*double(f.v.x)),
                                 float(double(f.corner.y)+a*double(f.u.y)+b*double(f.v.y))};
                require(sourceFaceRaySeparation(point,sunZ,f.corner,f.u,f.v)>0,
                        "represented face corners and interior must remain visible to rays");
                ++points;thinPoints+=std::abs(determinant)<1e-4f;
                memberships+=checkMembership(point);
            }
            const vec2 far{float(double(f.corner.x)+4.0*f.u.x+4.0*f.v.x),
                           float(double(f.corner.y)+4.0*f.u.y+4.0*f.v.y)};
            require(sourceFaceRaySeparation(far,sunZ,f.corner,f.u,f.v)==-1,
                    "far tolerance-only footprint must be rejected");
            ++farRejected;
        }
    }
    require(points>10000&&thinPoints>1000&&memberships>1000&&
            positiveDeterminants>0&&negativeDeterminants>0,
            "geometry fixture includes thin nonzero faces and both determinant signs");
    std::cout<<"represented_points="<<points<<" thin_points="<<thinPoints
             <<" far_rejected="<<farRejected<<" memberships="<<memberships<<"\n";
}
int main(){workload(MEASURE_ONLY==0);if(!MEASURE_ONLY){envelopeGeometry();geometry();}}
"""


def float_literals(source):
    return re.sub(r"(?<![\w.])((?:\d+\.\d*|\.\d+)(?:[eE][+-]?\d+)?|"
                  r"\d+[eE][+-]?\d+)(?![\w.])", r"\1f", source)


def host_source(backend, measure_only=False, contracted=None, envelope_order=None):
    shaders = ROOT / "engine/render/src/shaders" / ("metal" if backend == "metal" else "")
    layout = load_shader(shaders, "ir_sun_face_query_layout", backend)
    indexer = load_shader(shaders, "ir_sun_face_index", backend)
    projection = extract_function((shaders / f"ir_sun_projection.{backend}").read_text(),
                                  "sunSpaceProject")
    sampler = (shaders / f"ir_sun_shadow_sample.{backend}").read_text()
    tile = next(line.strip() for line in sampler.splitlines()
                if "const " in line and " tile = " in line)
    call = ("indexSourceSunFace(corner,u,v);" if backend == "glsl" else
            "indexSourceSunFace(sunDepthBuf.data,corner,u,v,sunFrame);")
    cases = (FIXTURE.replace("PROJECTION", float_literals(projection)).replace("CALL_INDEX", call)
             .replace("QUERY_TILE", tile))
    preamble = PREAMBLE.replace("struct vec3 {", "struct vec3 {\n"
                               "vec3(float a,float b,float c):x(a),y(b),z(c){}")
    source = (preamble + f"\n#define MEASURE_ONLY {int(measure_only)}\n"
              + shader_to_cpp(float_literals(layout + indexer) + cases))
    if contracted:
        name = "sourceFaceRaySeparation" if contracted == "receiver" else "indexSourceSunFace"
        start = source.rindex(" " + name + "(")
        tail = source[start:]
        tail = tail.replace("projectedFaceDeterminant(", "contractedDeterminant(", 1)
        source = source[:start] + tail
        definition = ("float contractedDeterminant(vec2 u,vec2 v){"
                      "return std::fma(u.x,v.y,-u.y*v.x);}\n")
        source = source.replace("#define MEASURE_ONLY", definition + "#define MEASURE_ONLY", 1)
    if envelope_order:
        envelope = extract_function(source, "sourceFaceWithinProjectedBounds")
        changed = envelope
        if envelope_order == "contracted":
            changed = changed.replace(
                "relativeMargin * (abs(corner) + abs(edgeU) + abs(edgeV)) + absoluteMargin",
                "std::fma(relativeMargin, (abs(corner) + abs(edgeU) + abs(edgeV)), absoluteMargin)")
        else:
            changed = changed.replace("abs(corner) + abs(edgeU) + abs(edgeV)",
                                      "abs(corner) + (abs(edgeU) + abs(edgeV))")
            for comparison in ("<", ">"):
                terms = (f"(edgeU {comparison} 0.0f ? edgeU : 0.0f) + "
                         f"(edgeV {comparison} 0.0f ? edgeV : 0.0f)")
                assert terms in changed
                changed = changed.replace(terms, "(" + terms + ")")
        assert changed != envelope
        source = source.replace(envelope, changed)
    return source


def compile_run(source):
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary)
        cpp, binary = path / "grazing.cpp", path / "grazing"
        cpp.write_text(source)
        build = subprocess.run([COMPILER, "-std=c++17", "-O2", "-ffp-contract=off", str(cpp),
                                "-o", str(binary)], capture_output=True, text=True)
        if build.returncode:
            raise RuntimeError(build.stderr)
        return subprocess.run([str(binary)], capture_output=True, text=True)


@unittest.skipUnless(COMPILER, "grazing index controls require a C++ compiler")
class GrazingFaceIndexTest(unittest.TestCase):
    def test_finite_grid_workload(self):
        for backend in ("glsl", "metal"):
            for contracted in (None, "receiver", "producer"):
                with self.subTest(backend=backend, contracted=contracted):
                    run = compile_run(host_source(backend, contracted=contracted))
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                    if contracted is None:
                        print(f"{backend}:\n{run.stdout}", end="")

    def test_receiver_endpoint_arithmetic(self):
        for backend in ("glsl", "metal"):
            original = host_source(backend)
            for order in ("contracted", "reassociated"):
                with self.subTest(backend=backend, order=order):
                    source = host_source(backend, envelope_order=order)
                    envelope = extract_function(source, "sourceFaceWithinProjectedBounds")
                    original_envelope = extract_function(
                        original, "sourceFaceWithinProjectedBounds")
                    self.assertEqual(source.replace(envelope, original_envelope), original)
                    run = compile_run(source)
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_rejection_and_workload_positive_controls(self):
        for backend in ("glsl", "metal"):
            source = host_source(backend)
            receiver = extract_function(source, "sourceFaceRaySeparation")
            envelope = extract_function(source, "sourceFaceWithinProjectedBounds")
            angular = receiver.replace("abs(determinant) < 0.000001f",
                                       "abs(determinant) < 0.001f")
            self.assertNotEqual(angular, receiver)
            variants = {
                "angular_rejection": (
                    source.replace(receiver, angular), "represented face corners and interior"),
                "unbounded_receiver": (
                    source.replace(envelope, envelope[:envelope.index("{") + 1] + "return true;}"),
                    "far tolerance-only footprint"),
                "full_cascade_index": (
                    source.replace("0u, low, high", "0u, vec2(-3e38), vec2(3e38)"),
                    "finite index workload"),
            }
            for name, (candidate, failure) in variants.items():
                with self.subTest(backend=backend, mutation=name):
                    self.assertNotEqual(candidate, source)
                    run = compile_run(candidate)
                    self.assertNotEqual(run.returncode, 0, f"mutation survived: {name}")
                    self.assertIn(failure, run.stderr)


if __name__ == "__main__":
    if sys.argv[1:] == ["--baseline"]:
        for backend in ("glsl", "metal"):
            result = compile_run(host_source(backend, measure_only=True))
            print(f"{backend}:\n{result.stdout}", end="")
            if result.returncode:
                raise SystemExit(result.stderr)
    else:
        unittest.main()
