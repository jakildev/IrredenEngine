"""Require every accepted in-map face query to have an indexed candidate.

Production index/query bodies run over the serial atomic adapter. Receivers are
formed independently in double precision and rounded around binary32 boundaries;
this checks membership, not GPU publication ordering or rasterized visibility.
"""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_block, extract_function
from test_render_source_face_index import COMPILER, PREAMBLE, ROOT, load_shader, shader_to_cpp

CASES = r"""
std::vector<uint> storage(kSourceFaceBufferWords,0xccccccccu);
uint accepted=0,rejected=0,outside=0,incomplete=0,failures=0,fixtures=0;
uint nearDegenerateAccepted=0,negativeAccepted=0,zeroAccepted=0;
void reset(bool exhausted){
    sunDepthBuf={storage.data(),kSourceFaceBufferWords};
    sunDepthBuf[kSourceFaceHeaderOffset]=exhausted?kSourceFaceCapacity:0;
    sunDepthBuf[kSourceFaceSpillCounterOffset]=0;
    for(uint tile=0;tile<kSourceFaceTileCount;++tile){
        sunDepthBuf[sourceFaceTileBase(tile)]=0;
        sunDepthBuf[kSourceFaceSpillHeadOffset+tile]=0xffffffffu;
    }
}
void configure(vec2 origin0,vec2 origin1,vec2 texel0,vec2 texel1){
    cascadeOriginUV_0=origin0;cascadeOriginUV_1=origin1;
    cascadeTexelSize_0=texel0;cascadeTexelSize_1=texel1;
    sunFrame.cascadeOriginUV_0=origin0;sunFrame.cascadeOriginUV_1=origin1;
    sunFrame.cascadeTexelSize_0=texel0;sunFrame.cascadeTexelSize_1=texel1;
}
float adjacent(float value,int direction){
    if(direction==0)return value;
    return std::nextafter(value,direction<0?-INFINITY:INFINITY);
}
void query(vec3 corner,vec3 u,vec3 v,vec2 sunUV,bool exhausted){
    const float separation=sourceFaceRaySeparation(sunUV,2,corner,u,v);
    if(separation!=2){++rejected;return;}
    for(uint cascade=0;cascade<2;++cascade){
        const vec2 origin=cascade==0?cascadeOriginUV_0:cascadeOriginUV_1;
        const vec2 texelSz=cascade==0?cascadeTexelSize_0:cascadeTexelSize_1;
        QUERY_TILE
        if(tile.x<0||tile.y<0||tile.x>=int(kSourceFaceTilesPerAxis)||
           tile.y>=int(kSourceFaceTilesPerAxis)){++outside;continue;}
        ++accepted;
        nearDegenerateAccepted+=abs(projectedFaceDeterminant(u.xy(),v.xy()))<.0001f;
        negativeAccepted+=sunUV.x<0||sunUV.y<0;
        zeroAccepted+=sunUV.x==0||sunUV.y==0;
        const uint tileIndex=cascade*kSourceFaceTilesPerAxis*kSourceFaceTilesPerAxis+
                             uint(tile.y)*kSourceFaceTilesPerAxis+uint(tile.x);
        const uint base=sourceFaceTileBase(tileIndex),count=sunDepthBuf[base];
        if(!sourceFaceQueryComplete(count)){++incomplete;continue;}
        bool found=false;
        for(uint i=0;i<min(count,kSourceFaceTileCapacity);++i)
            found=found||sunDepthBuf[base+1+i]==0;
        if(!found||exhausted){
            if(failures++<8)
                std::cerr<<"accepted receiver missing from complete tile: fixture="<<fixtures
                         <<" cascade="<<cascade<<" tile="<<tile.x<<","<<tile.y
                         <<" count="<<count<<" receiver="<<sunUV.x<<","<<sunUV.y
                         <<" corner="<<corner.x<<","<<corner.y
                         <<" determinant="<<projectedFaceDeterminant(u.xy(),v.xy())<<"\n";
        }
    }
}
void face(vec3 corner,vec3 u,vec3 v,bool exhausted=false){
    reset(exhausted);++fixtures;
    CALL_INDEX
    // Binary64 point construction does not share the shader's edge arithmetic.
    for(double a:{0.0,.5,1.0})for(double b:{0.0,.5,1.0}){
        const float x=float(double(corner.x)+a*double(u.x)+b*double(v.x));
        const float y=float(double(corner.y)+a*double(u.y)+b*double(v.y));
        for(int dx:{-1,0,1})for(int dy:{-1,0,1})
            query(corner,u,v,{adjacent(x,dx),adjacent(y,dy)},exhausted);
    }
    query(corner,u,v,{corner.x+u.x*4+v.x*4,corner.y+u.y*4+v.y*4},exhausted);
}
int main(){
    configure({0,0},{-8,-8},{1,1},{2,3});
    for(bool exhausted:{false,true}){
        reset(exhausted);++fixtures;
        const vec3 corner{std::nextafter(8.0f,9.0f),0,0},u{1,0,0},v{0,1,0};
        CALL_INDEX
        query(corner,u,v,{std::nextafter(8.0f,7.0f),.5f},exhausted);
    }
    const vec2 origins[][2]={{{0,0},{-64,-48}},{{-64,-32},{128,-128}},
                            {{96,64},{-1024,-512}}};
    const vec2 texels[][2]={{{1,1},{.5f,2}},{{.25f,3},{2,.125f}},
                           {{1.5f,.75f},{4,2}}};
    const vec3 edges[][2]={{{1,0,0},{0,1,0}},{{1,.25f,0},{-.5f,1,0}},
                          {{.70710677f,.70710677f,0},{-.70710677f,.70710677f,0}},
                          {{1,0,0},{.75f,2e-6f,0}},{{8,1,0},{8,1.000001f,0}}};
    for(uint config=0;config<3;++config){
        configure(origins[config][0],origins[config][1],texels[config][0],texels[config][1]);
        for(uint cascade=0;cascade<2;++cascade)for(int boundary:{0,1,8,127,128}){
            const vec2 origin=origins[config][cascade],texel=texels[config][cascade];
            const vec2 anchor{origin.x+boundary*8*texel.x,origin.y+boundary*8*texel.y};
            for(const auto& basis:edges)for(int winding=0;winding<8;++winding)
            for(int shift:{-1,0,1}){
                vec3 u=basis[0],v=basis[1];
                if(winding&1)std::swap(u,v);
                if(winding&2)u={-u.x,-u.y,0};
                if(winding&4)v={-v.x,-v.y,0};
                face({adjacent(anchor.x,shift),adjacent(anchor.y,-shift),0},u,v);
            }
        }
        // Zero and negative zero can be internal tile boundaries in translated cascades.
        for(float coordinate:{-8.0f,-0.0f,0.0f,8.0f})
            for(const auto& basis:edges)face({coordinate,coordinate,0},basis[0],basis[1]);
    }
    if(accepted<10000||rejected==0||outside==0||incomplete==0||
       nearDegenerateAccepted==0||negativeAccepted==0||zeroAccepted==0){
        std::cerr<<"fixture coverage is incomplete\n";return 2;
    }
    std::cout<<fixtures<<" faces; "<<accepted<<" accepted in-map queries; "
             <<rejected<<" rejected; "<<outside<<" outside; "<<incomplete
             <<" incomplete; "<<failures<<" membership failures\n";
    return failures?1:0;
}
"""


UNSAFE_CASES = CASES[:CASES.index("int main(){")] + r"""
void require(bool value,const char* label){
    if(!value){std::cerr<<label<<"\n";std::exit(3);}
}
void fullCascade(vec3 corner,vec3 u,vec3 v,uint cascadeMask=3){
    reset(false);++fixtures;
    CALL_INDEX
    require(sunDepthBuf[kSourceFaceHeaderOffset]==1,"unsafe geometry publishes one record");
    for(uint cascade=0;cascade<2;++cascade){
        if((cascadeMask&(1u<<cascade))==0)continue;
        for(uint tile=0;tile<128*128;++tile){
            const uint base=sourceFaceTileBase(cascade*128*128+tile);
            require(sunDepthBuf[base]==1&&sunDepthBuf[base+1]==0,
                    "unsafe arithmetic requires full-cascade membership");
        }
    }
}
int main(){
    const float minimumNormal=std::ldexp(1.0f,-126);
    const float subnormal=std::nextafter(0.0f,1.0f);
    for(float tiny:{0.0f,-0.0f,subnormal,-subnormal,minimumNormal,-minimumNormal}){
        require(sourceFaceBoundDown(tiny)<=-minimumNormal&&
                sourceFaceBoundUp(tiny)>=minimumNormal,"underflow enclosure");
    }
    configure({0,0},{-8,-8},{1,1},{2,3});
    for(float tiny:{subnormal,-subnormal})
        fullCascade({tiny,0,0},{1,0,0},{0,1,0});
    fullCascade({0,0,0},{std::ldexp(1.0f,61),0,0},{0,1,0});
    fullCascade({0,0,0},{std::ldexp(1.0f,100),0,0},{0,std::ldexp(1.0f,100),0});
    query({0,0,0},{std::ldexp(1.0f,100),0,0},{0,std::ldexp(1.0f,100),0},{.5f,.5f},false);

    const float large=std::ldexp(1.0f,61);
    configure({large,0},{large,-8},{1,1},{2,3});
    fullCascade({large,0,0},{1,0,0},{0,1,0});
    query({large,0,0},{1,0,0},{0,1,0},{large,.5f},false);
    for(float origin:{std::ldexp(1.0f,59),-std::ldexp(1.0f,59),subnormal,-subnormal}){
        configure({origin,0},{-8,-8},{1,1},{2,3});
        fullCascade({0,0,0},{1,0,0},{0,1,0},1);
    }
    for(float texel:{0.0f,-1.0f,subnormal,std::ldexp(1.0f,48),std::ldexp(1.0f,125)}){
        configure({0,0},{-8,-8},{texel,1},{2,3});
        fullCascade({0,0,0},{1,0,0},{0,1,0},1);
    }
    configure({0,0},{-8,-8},{minimumNormal/8,minimumNormal/8},{2,3});
    fullCascade({0,0,0},{1,0,0},{0,1,0},1);
    query({0,0,0},{1,0,0},{0,1,0},{0,0},false);
    require(accepted>=5&&failures==0,"extreme finite receivers retain indexed candidates");
    std::cout<<fixtures<<" unsafe geometry/cascade fixtures; "<<accepted
             <<" accepted extreme receivers\n";
}
"""


MIXED_ARITHMETIC_CASES = CASES[:CASES.index("int main(){")] + r"""
int main(){
    configure({0,0},{0,0},{1,1},{1,1});
    reset(false);
    const vec3 corner{0,0,0};
    const vec3 u{8.12339973449707f,11.12345027923584f,0};
    const vec3 v{16.24679946899414f,22.24690055847168f,0};
    const float producer=projectedFaceDeterminant(u.xy(),v.xy());
    const float receiver=std::fma(u.x,v.y,-(u.y*v.x));
    if(producer!=0||abs(receiver)<.000001f){
        std::cerr<<"mixed determinant fixture lost its rounding difference\n";return 2;
    }
    CALL_INDEX
    if(sourceFaceRaySeparation({0,0},2,corner,u,v)!=2){
        std::cerr<<"contracted receiver must accept the face corner\n";return 2;
    }
    query(corner,u,v,{0,0},false);
    if(accepted!=2||failures){
        std::cerr<<"mixed arithmetic omitted an accepted receiver\n";return 1;
    }
    std::cout<<"uncontracted producer and contracted receiver retain both memberships\n";
}
"""


CAPACITY_CASES = CASES[:CASES.index("int main(){")] + r"""
void require(bool value,const char* label){
    if(!value){std::cerr<<label<<"\n";std::exit(3);}
}
int main(){
    constexpr uint unwritten=0xccccccccu;
    configure({0,0},{-8,-8},{1,1},{2,3});
    const vec3 corner{std::nextafter(0.0f,1.0f),0,0},u{1,0,0},v{0,1,0};

    reset(true);
    std::fill(storage.begin()+kSourceFaceRecordOffset,
              storage.begin()+kSourceFaceSpillCounterOffset,unwritten);
    CALL_INDEX
    require(sunDepthBuf[kSourceFaceHeaderOffset]==kSourceFaceCapacity+1,
            "full-cascade exhausted face reserves once");
    require(sunDepthBuf[kSourceFaceSpillCounterOffset]==0,
            "exhausted records must not allocate spill nodes");
    for(uint word=kSourceFaceRecordOffset;word<kSourceFaceSpillCounterOffset;++word)
        require(sunDepthBuf[word]==unwritten,"exhausted records must not write geometry");
    for(uint tile=0;tile<kSourceFaceTileCount;++tile)
        require(!sourceFaceQueryComplete(sunDepthBuf[sourceFaceTileBase(tile)]) &&
                !sourceFaceTileRetainsRecords(sunDepthBuf[sourceFaceTileBase(tile)]),
                "exhausted full-cascade face must mark every tile as missing records");

    reset(false);
    sunDepthBuf[kSourceFaceHeaderOffset]=kSourceFaceTileCapacity;
    for(uint tile=0;tile<kSourceFaceTileCount;++tile){
        const uint base=sourceFaceTileBase(tile);
        sunDepthBuf[base]=kSourceFaceTileCapacity;
        for(uint entry=0;entry<kSourceFaceTileCapacity;++entry)
            sunDepthBuf[base+1+entry]=entry;
    }
    CALL_INDEX
    require(sunDepthBuf[kSourceFaceHeaderOffset]==kSourceFaceTileCapacity+1,
            "full-cascade spill face reserves once");
    require(sunDepthBuf[kSourceFaceSpillCounterOffset]==kSourceFaceTileCount,
            "full-cascade face allocates one spill node per full primary list");
    std::vector<bool> seen(kSourceFaceTileCount,false);
    for(uint tile=0;tile<kSourceFaceTileCount;++tile){
        const uint base=sourceFaceTileBase(tile),count=sunDepthBuf[base];
        require(count==kSourceFaceTileCapacity+1&&sourceFaceQueryComplete(count),
                "full-cascade spill list stays complete");
        for(uint entry=0;entry<kSourceFaceTileCapacity;++entry)
            require(sunDepthBuf[base+1+entry]==entry,"spill must preserve primary records");
        const uint spill=sunDepthBuf[kSourceFaceSpillHeadOffset+tile];
        require(spill<seen.size()&&!seen[spill],"spill nodes must be distinct and in bounds");
        seen[spill]=true;
        const uint node=kSourceFaceSpillNodeOffset+spill*kSourceFaceSpillNodeWords;
        require(sunDepthBuf[node]==kSourceFaceTileCapacity&&sunDepthBuf[node+1]==0xffffffffu,
                "complete list must publish the new face through its spill chain");
    }

    reset(false);
    const uint target=kSourceFaceTileCount-1;
    const uint base=sourceFaceTileBase(target);
    const uint spillCount=kSourceFaceTileQueryCapacity-kSourceFaceTileCapacity;
    sunDepthBuf[kSourceFaceHeaderOffset]=kSourceFaceTileQueryCapacity;
    sunDepthBuf[kSourceFaceSpillCounterOffset]=spillCount;
    sunDepthBuf[base]=kSourceFaceTileQueryCapacity;
    for(uint entry=0;entry<kSourceFaceTileCapacity;++entry)
        sunDepthBuf[base+1+entry]=entry;
    sunDepthBuf[kSourceFaceSpillHeadOffset+target]=spillCount-1;
    for(uint spill=0;spill<spillCount;++spill){
        const uint node=kSourceFaceSpillNodeOffset+spill*kSourceFaceSpillNodeWords;
        sunDepthBuf[node]=kSourceFaceTileCapacity+spill;
        sunDepthBuf[node+1]=spill==0?0xffffffffu:spill-1;
    }
    CALL_INDEX
    require(sunDepthBuf[kSourceFaceHeaderOffset]==kSourceFaceTileQueryCapacity+1,
            "query-capacity face reserves once");
    require(sunDepthBuf[base]==kSourceFaceTileQueryCapacity+1&&
            !sourceFaceQueryComplete(sunDepthBuf[base]),
            "full query list must become incomplete");
    require(sunDepthBuf[kSourceFaceSpillCounterOffset]==spillCount&&
            sunDepthBuf[kSourceFaceSpillHeadOffset+target]==spillCount-1,
            "query-capacity overflow must not allocate or republish spill nodes");
    for(uint entry=0;entry<kSourceFaceTileCapacity;++entry)
        require(sunDepthBuf[base+1+entry]==entry,"query overflow preserves primary records");
    for(uint spill=0;spill<spillCount;++spill){
        const uint node=kSourceFaceSpillNodeOffset+spill*kSourceFaceSpillNodeWords;
        require(sunDepthBuf[node]==kSourceFaceTileCapacity+spill&&
                sunDepthBuf[node+1]==(spill==0?0xffffffffu:spill-1),
                "query overflow preserves the existing spill chain");
    }
    for(uint tile=0;tile<target;++tile){
        const uint other=sourceFaceTileBase(tile);
        require(sunDepthBuf[other]==1&&sunDepthBuf[other+1]==kSourceFaceTileQueryCapacity,
                "one saturated tile must not demote other full-cascade memberships");
    }
    std::cout<<"full-cascade record exhaustion, primary spill and query overflow passed\n";
}
"""


def host_source(backend, raw_bounds=False, cases=CASES):
    folder = "metal" if backend == "metal" else ""
    shaders = ROOT / "engine/render/src/shaders" / folder
    layout = load_shader(shaders, "ir_sun_face_query_layout", backend)
    indexer = load_shader(shaders, "ir_sun_face_index", backend)
    if raw_bounds:
        if backend == "metal":
            signature = re.search(
                r"inline void indexSourceSunFace\([^\n]*sunFrame\) \{", indexer)
            if signature is None:
                raise ValueError("missing serial Metal face index signature")
            body = extract_block(indexer, signature[0])
        else:
            body = extract_function(indexer, "indexSourceSunFace")
        opening = body.index("{")
        arguments = ("sunDepthBuf, corner, edgeU, edgeV, 0u, low, high, sunFrame"
                     if backend == "metal" else "corner, edgeU, edgeV, 0u, low, high")
        scalar = "float2" if backend == "metal" else "vec2"
        replacement = body[:opening + 1] + ("\n#endif" if backend == "metal" else "") + f"""
    if (abs(projectedFaceDeterminant(edgeU.xy, edgeV.xy)) < 0.000001) return;
    const {scalar} low = min(min(corner.xy, corner.xy + edgeU.xy),
        min(corner.xy + edgeV.xy, corner.xy + edgeU.xy + edgeV.xy));
    const {scalar} high = max(max(corner.xy, corner.xy + edgeU.xy),
        max(corner.xy + edgeV.xy, corner.xy + edgeU.xy + edgeV.xy));
    indexSourceSunGeometry({arguments});
}}
"""
        indexer = indexer.replace(body, replacement)
    sampler = (shaders / f"ir_sun_shadow_sample.{backend}").read_text()
    tile = re.search(r"const (?:ivec2|int2) tile = [^;]+;", sampler)
    if tile is None:
        raise ValueError("missing production receiver tile calculation")
    call = ("indexSourceSunFace(corner,u,v);" if backend == "glsl" else
            "indexSourceSunFace(sunDepthBuf.data,corner,u,v,sunFrame);")
    source = (layout + "\n" + indexer
              + cases.replace("CALL_INDEX", call).replace("QUERY_TILE", tile[0]))
    source = shader_to_cpp(source)
    preamble = PREAMBLE.replace("const vec2 cascade", "vec2 cascade")
    return preamble + "\nusing std::min;\n" + source


@unittest.skipUnless(COMPILER, "index/query coverage controls require a C++ compiler")
class SourceFaceIndexCoverageTest(unittest.TestCase):
    def compile_and_run(self, source):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            cpp, executable = path / "coverage.cpp", path / "coverage"
            cpp.write_text(source)
            build = subprocess.run(
                [COMPILER, "-std=c++17", "-O2", "-ffp-contract=off", str(cpp),
                 "-o", str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            return subprocess.run([str(executable)], capture_output=True, text=True)

    def test_mixed_determinant_contraction_preserves_membership(self):
        for backend in ("glsl", "metal"):
            source = host_source(backend, cases=MIXED_ARITHMETIC_CASES)
            receiver = extract_function(source, "sourceFaceRaySeparation")
            contracted, replacements = re.subn(
                r"const float determinant = projectedFaceDeterminant\([^;]+;",
                "const float determinant = std::fma(edgeU.x, edgeV.y, -(edgeU.y * edgeV.x));",
                receiver)
            self.assertEqual(replacements, 1)
            source = source.replace(receiver, contracted)
            mutant, replacements = re.subn(
                r"if \(sourceFaceDefinitelyDegenerate\([^;]+?\)\) return;",
                "if (abs(determinant) < 0.000001) return;", source)
            self.assertEqual(replacements, 1, "expected conservative producer degeneracy gate")
            for name, variant in (("production", source), ("rounded_early_skip", mutant)):
                with self.subTest(backend=backend, mutation=name):
                    run = self.compile_and_run(variant)
                    self.assertEqual(run.returncode, 0 if name == "production" else 1,
                                     run.stdout + run.stderr)
                    if name != "production":
                        self.assertIn("mixed arithmetic omitted an accepted receiver", run.stderr)
                    else:
                        print(f"{backend}: {run.stdout}", end="")

    def test_full_cascade_capacity_transitions(self):
        for backend in ("glsl", "metal"):
            source = host_source(backend, cases=CAPACITY_CASES)
            mark_incomplete = ("atomicOr(sunDepthBuf[base], kSourceFaceTileRecordLost);"
                               if backend == "glsl" else
                               "atomic_fetch_or_explicit(&sunDepthBuf[base], "
                               "kSourceFaceTileRecordLost, memory_order_relaxed);")
            mutations = {
                "missing_record_exhaustion": (mark_incomplete, ";"),
                "primary_off_by_one": ("slot < kSourceFaceTileCapacity",
                                       "slot <= kSourceFaceTileCapacity"),
                "query_off_by_one": ("slot < kSourceFaceTileQueryCapacity",
                                     "slot <= kSourceFaceTileQueryCapacity"),
            }
            for name, mutation in {"production": None, **mutations}.items():
                with self.subTest(backend=backend, mutation=name):
                    variant = source
                    if mutation:
                        old, new = mutation
                        self.assertIn(old, source)
                        variant = source.replace(old, new)
                    run = self.compile_and_run(variant)
                    self.assertEqual(run.returncode, 3 if mutation else 0,
                                     run.stdout + run.stderr)
                    if not mutation:
                        print(f"{backend}: {run.stdout}", end="")

    def test_every_accepted_receiver_has_a_candidate_or_incomplete_tile(self):
        for backend in ("glsl", "metal"):
            for raw_bounds in (False, True):
                with (self.subTest(backend=backend, raw_bounds=raw_bounds),
                      tempfile.TemporaryDirectory() as temporary):
                    path = Path(temporary)
                    cpp, executable = path / "coverage.cpp", path / "coverage"
                    cpp.write_text(host_source(backend, raw_bounds))
                    build = subprocess.run(
                        [COMPILER, "-std=c++17", "-O2", "-ffp-contract=off", str(cpp),
                         "-o", str(executable)], capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stderr)
                    run = subprocess.run([str(executable)], capture_output=True, text=True)
                    self.assertEqual(run.returncode, 1 if raw_bounds else 0,
                                     run.stdout + run.stderr)
                    if raw_bounds:
                        self.assertIn("accepted receiver missing from complete tile", run.stderr)
                        print(f"{backend} raw-AABB mutation: {run.stdout}", end="")
                    else:
                        print(f"{backend}: {run.stdout}", end="")

    def test_unsafe_arithmetic_indexes_full_cascades(self):
        mutations = {
            "production": None,
            "missing_cascade_safety": (
                "!sourceFaceCascadeArithmeticSafe(origin, cellSize)", "false"),
            "missing_geometry_fallback": (
                "low = vec2(-3.4028234663852886e38);\n"
                "        high = vec2(3.4028234663852886e38);", "low = high = vec2(0.0);"),
            "lost_underflow_enclosure": (
                "magnitude <= 0x00800000u", "false"),
        }
        for backend in ("glsl", "metal"):
            original = host_source(backend, cases=UNSAFE_CASES)
            for name, mutation in mutations.items():
                with (self.subTest(backend=backend, mutation=name),
                      tempfile.TemporaryDirectory() as temporary):
                    source = original
                    if mutation:
                        old, new = mutation
                        self.assertIn(old, source)
                        source = source.replace(old, new)
                    path = Path(temporary)
                    cpp, executable = path / "unsafe.cpp", path / "unsafe"
                    cpp.write_text(source)
                    build = subprocess.run(
                        [COMPILER, "-std=c++17", "-O2", "-ffp-contract=off", str(cpp),
                         "-o", str(executable)], capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stderr)
                    run = subprocess.run([str(executable)], capture_output=True, text=True)
                    self.assertEqual(run.returncode, 3 if mutation else 0,
                                     run.stdout + run.stderr)
                    if mutation:
                        self.assertIn("underflow enclosure" if name == "lost_underflow_enclosure"
                                      else "full-cascade membership", run.stderr)
                    else:
                        print(f"{backend}: {run.stdout}", end="")


if __name__ == "__main__":
    unittest.main()
