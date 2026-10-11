"""Execute torus sampling/publication and independently check finite-face rays."""

import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_block, extract_function

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import render_torus_surface_probe as probe  # noqa: E402

SHADERS = probe.ROOT / "engine/render/src/shaders"
COMPILER = shutil.which("c++")
PREAMBLE = r"""
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <tuple>
#include <vector>
using uint=unsigned;
template<class T>struct V2 {T x,y;V2(T a,T b):x(a),y(b){}};
using vec2=V2<float>;using ivec2=V2<int>;
struct vec3 {float x,y,z;vec3(float a,float b,float c):x(a),y(b),z(c){}
 vec2 xy()const{return {x,y};}};
struct vec4 {float x,y,z,w;};
float length(vec2 v){return std::sqrt(v.x*v.x+v.y*v.y);}
const int kXFace=0,kYFace=1,kZFace=2,kInvalidDepth=999999,kDepthEncodeShift=8;
using Candidate=std::tuple<int,uint,int,int,int,int>;
uint owner(int i,int j,int face,int subPixel){
 uint x=i+104,y=j+136,tileIdx=(y/8)*26+x/8,gl_LocalInvocationIndex=(y%8)*8+x%8;
 struct {uint x,y;} localId{gl_LocalInvocationIndex%8,gl_LocalInvocationIndex/8};
 return OWNER;
}
bool publishes(int depthEncoded,int stored,uint sampleOwner,uint elected){return PREDICATE;}
void fixtureAtomicMin(uint& target,uint value){target=std::min(target,value);}
void fixtureAtomicMax(uint& target,uint value){target=std::max(target,value);}
void fixtureAtomicMin(int& target,int value){target=std::min(target,value);}
void fixtureAtomicMax(int& target,int value){target=std::max(target,value);}
void elect(int depthEncoded,int stored,uint sampleOwner,uint& elected) ELECTION
void storeDepth(int depthEncoded,int& stored) DEPTH_ELECTION
"""
MAIN = r"""
int main(){
 std::vector<std::pair<std::pair<int,int>,Candidate>> candidates;
 for(int j=-132;j<=132;++j)for(int i=-100;i<=100;++i){
  int d=generalDepthSearch(ivec2(i,j),0,vec4{32,16,0,0},false,115);
  if(d==kInvalidDepth)continue;
  std::printf("S %d %d %d\n",i,j,d);
  for(int face=0;face<3;++face)for(int half=0;half<2;++half){
   auto offset=faceOffset_2x3(face,half);
   candidates.push_back({{i+offset.x,j+offset.y},
    {encodeDepthWithFace(d,face,0),owner(i,j,face,half),i,j,face,half}});
  }
 }
 for(int face=0;face<3;++face){auto n=faceOutwardNormal(face);
  std::printf("N %d %.0f %.0f %.0f\n",face,n.x,n.y,n.z);}
 std::map<std::pair<int,int>,Candidate> previous;
 std::mt19937 rng(72591);
 for(int trial=0;trial<4;++trial){
  std::shuffle(candidates.begin(),candidates.end(),rng);
  std::map<std::pair<int,int>,int> depths;
  std::map<std::pair<int,int>,uint> owners;
  for(auto item:candidates){auto key=item.first;int d=std::get<0>(item.second);
   if(!depths.count(key))depths.emplace(key,0x7fffffff);
   storeDepth(d,depths.at(key));}
  for(auto item:candidates){auto key=item.first;auto c=item.second;
   uint id=std::get<1>(c);
   if(!owners.count(key))owners.emplace(key,0xffffffffu);
   elect(std::get<0>(c),depths.at(key),id,owners.at(key));}
  std::map<std::pair<int,int>,Candidate> winners;
  for(auto item:candidates){auto key=item.first;auto c=item.second;
   if(publishes(std::get<0>(c),depths.at(key),std::get<1>(c),owners.at(key))){
    if(winners.count(key))return 3;
    winners.emplace(key,c);
   }}
  if(trial&&winners!=previous)return 4;
  previous=winners;
 }
 if(publishes(9,8,1,1)||publishes(8,8,2,1))return 5;
 for(auto item:previous){auto c=item.second;
  std::printf("W %d %d %d %u %d %d %d %d\n",item.first.first,item.first.second,
   std::get<0>(c),std::get<1>(c),std::get<2>(c),std::get<3>(c),std::get<4>(c),std::get<5>(c));}
}
"""


def program(backend):
    folder = SHADERS / ("metal" if backend == "metal" else "")
    emitter = (folder / f"c_shapes_to_trixel_body.{backend}").read_text()
    iso = (folder / f"ir_iso_common.{backend}").read_text()
    sdf = (folder / f"ir_sdf_common.{backend}").read_text()
    key = re.search(r"const uint sampleOwner = ([^;]+);", emitter).group(1)
    predicate = re.search(r"if \((depthEncoded == stored &&.*?)\) \{",
                          emitter, re.DOTALL).group(1)
    predicate = re.sub(
        r"atomic_load_explicit\(&sampleOwners\[linearIndex\], memory_order_relaxed\)",
        "elected", predicate).replace("sampleOwners[linearIndex]", "elected")
    election = extract_block(emitter, "#if IR_SHAPE_PASS == 3")
    election = election[election.index("{"):]
    election = re.sub(r"imageLoad\(triangleCanvasDistances, canvasPixel\).r", "stored", election)
    election = election.replace(
        "atomic_load_explicit(&distanceScratch[linearIndex], memory_order_relaxed)", "stored")
    election = election.replace("sampleOwners[linearIndex]", "elected")
    election = election.replace("atomicMin(elected, sampleOwner)",
                                 "fixtureAtomicMin(elected, sampleOwner)")
    election = election.replace(
        "atomic_fetch_min_explicit(&elected, sampleOwner, memory_order_relaxed)",
        "fixtureAtomicMin(elected, sampleOwner)")
    depth_election = extract_block(emitter, "#elif IR_SHAPE_PASS != 1")
    depth_election = depth_election[depth_election.index("{"):]
    depth_election = re.sub(
        r"imageAtomicMin\(triangleCanvasDistances, canvasPixel,\s*depthEncoded\)",
        "fixtureAtomicMin(stored, depthEncoded)", depth_election)
    depth_election = re.sub(
        r"atomic_fetch_min_explicit\(\s*&distanceScratch\[linearIndex\],\s*"
        r"depthEncoded,\s*memory_order_relaxed\s*\)",
        "fixtureAtomicMin(stored, depthEncoded)", depth_election)
    code = (PREAMBLE.replace("OWNER", key).replace("PREDICATE", predicate)
            .replace("DEPTH_ELECTION", depth_election).replace("ELECTION", election))
    code += re.search(r"(?:const|constant) float kSdfBiasEpsilon = [^;]+;", emitter).group()
    for name in ("isoPositionToPos3D", "isoPixelToPos3D", "isoToLocal3D"):
        code += extract_function(iso, name)
    code += extract_function(sdf, "sdfTorus")
    case = re.search(r"case SHAPE_TORUS:\s*return ([^;]+);", sdf).group(1)
    code += f"float evaluateSDF(vec3 localPos,uint,vec4 params){{return {case};}}"
    code += extract_function(emitter, "generalDepthSearch")
    code += extract_block(iso, "inline int2 faceOffset_2x3" if backend == "metal"
                          else "ivec2 faceOffset_2x3")
    code += extract_function(iso, "encodeDepthWithFace")
    code += extract_function(iso, "faceOutwardNormal")
    for a, b in (("float2", "vec2"), ("float3", "vec3"), ("float4", "vec4"),
                 ("int2", "ivec2"), ("constant ", "const ")):
        code = code.replace(a, b)
    return code.replace(".xy", ".xy()") + MAIN


@unittest.skipUnless(COMPILER, "shader contracts require a C++ compiler")
class TorusPublisherTest(unittest.TestCase):
    def test_production_and_mutations(self):
        expected_samples = probe.samples()
        expected_winners = {key: min(options)
                            for key, options in probe.candidates(expected_samples).items()}
        mutations = {
            "production": lambda s: s,
            "later_sample": lambda s: s.replace("return d;", "return d + 1;"),
            "wrong_half": lambda s: s.replace("1 + subPixel", "1 + (1 - subPixel)"),
            "wrong_normal": lambda s: s.replace("return vec3(-1.0, 0.0, 0.0)",
                                                  "return vec3(0.0, -1.0, 0.0)"),
            "reverse_owner": lambda s: s.replace("fixtureAtomicMin(elected, sampleOwner)",
                                                   "fixtureAtomicMax(elected, sampleOwner)"),
            "reverse_depth": lambda s: s.replace("fixtureAtomicMin(stored, depthEncoded)",
                                                   "fixtureAtomicMax(stored, depthEncoded)"),
            "missing_depth_gate": lambda s: s.replace("depthEncoded == stored &&", "true &&"),
        }
        for backend in ("glsl", "metal"):
            for name, mutate in mutations.items():
                with (self.subTest(backend=backend, mutation=name),
                      tempfile.TemporaryDirectory() as tmp):
                    original = program(backend)
                    source = mutate(original)
                    self.assertEqual(source == original, name == "production")
                    path = Path(tmp)
                    (path / "probe.cpp").write_text(source)
                    build = subprocess.run(
                        [COMPILER, "-std=c++17", "-O2", str(path / "probe.cpp"),
                         "-o", str(path / "probe")], capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stderr)
                    run = subprocess.run([str(path / "probe")], capture_output=True, text=True)
                    actual_samples, actual_winners, normals = {}, {}, {}
                    for line in run.stdout.splitlines():
                        tag, *numbers = line.split()
                        values = tuple(map(int, numbers))
                        if tag == "S":
                            actual_samples[values[:2]] = values[2]
                        elif tag == "W":
                            actual_winners[values[:2]] = values[2:]
                        else:
                            normals[values[0]] = values[1:]
                    agrees = (run.returncode == 0 and actual_samples == expected_samples
                              and actual_winners == expected_winners
                              and normals == {0: (-1, 0, 0), 1: (0, -1, 0), 2: (0, 0, -1)})
                    self.assertEqual(agrees, name == "production", name)


class FiniteFaceDiagnosticTest(unittest.TestCase):
    def test_image_gate_detects_extra_foreground_and_count_preserving_move(self):
        candidates = probe.candidates({(0, 0): 0})
        data = bytearray(16 * 16 * 3)
        for (i, j), options in candidates.items():
            for y in range(2):
                for x in range(4):
                    pixel = (8 + 4 * (i - 1) + x, 8 + 2 * (j - 1) + y)
                    start = (pixel[1] * 16 + pixel[0]) * 3
                    data[start:start + 3] = bytes(probe.NORMAL_COLORS[min(options)[4]])
        image = (16, 16, 3, data)
        correct = probe.compare_image(candidates, image)
        self.assertEqual(correct["matching_foreground_pixels"], 48)
        self.assertEqual(correct["unexpected_foreground_pixels"], 0)
        data[:3] = b"\x01\x02\x03"
        extra = probe.compare_image(candidates, image)
        self.assertEqual(extra["unexpected_foreground_pixels"], 1)
        self.assertFalse(extra["exact_full_image_color_counts"])
        foreground = (6 * 16 + 4) * 3
        data[:3], data[foreground:foreground + 3] = data[foreground:foreground + 3], b"\0\0\0"
        moved = probe.compare_image(candidates, image)
        self.assertTrue(moved["exact_full_image_color_counts"])
        self.assertEqual(moved["unexpected_foreground_pixels"], 1)
        self.assertEqual(moved["mismatching_foreground_pixels"], 1)

    def test_equal_depth_owners_remain_uncertain(self):
        accepted = {(0, 0): 0, (1, 0): 0}
        options = probe.candidates(accepted)[1, 0]
        result = probe.classify_possible_owners(accepted, options, .125, -.75)
        self.assertEqual(result["classification"], "owner_ambiguous")
        self.assertEqual({item["classification"] for item in result["possible_owners"]},
                         {"nearest_exposed_face", "publisher_face_not_on_ray"})

    def test_fractional_samples_and_occluded_faces(self):
        accepted = {(0, 0): 0, (0, 1): -2}
        self.assertEqual(probe.position(0, 1, -2), (-5 / 6, -5 / 6, -1 / 3))
        hits = probe.ray_hits(accepted, .1, .1)
        self.assertEqual(hits[0]["sample"], [0, 1, -2])
        self.assertLess(hits[0]["depth"], hits[-1]["depth"])
        selected = (0, 0, 0, 0, 0, 0)
        classification = probe.classify(accepted, selected, .1, .1)
        self.assertEqual(classification["classification"], "publisher_face_occluded")

    def test_edges_holes_and_wrong_face_control(self):
        accepted = {(0, 0): 0}
        hits = probe.ray_hits(accepted, 0, 0)
        self.assertEqual(hits[0]["axes"], [0, 1, 2])
        self.assertEqual(hits[0]["depth"], -1.5)
        self.assertEqual(probe.ray_hits(accepted, 2, 0), [])
        miss = probe.classify(accepted, (0, 0, 0, 0, 0, 0), 2, 0)
        self.assertIsNone(miss["normal_matches_nearest"])
        for face, uv in ((0, (.4, 0)), (1, (-.4, 0)), (2, (0, -.4))):
            good = probe.classify(accepted, (0, 0, 0, 0, face, 0), *uv)
            bad = probe.classify(accepted, (0, 0, 0, 0, (face + 1) % 3, 0), *uv)
            self.assertEqual(good["classification"], "nearest_exposed_face")
            self.assertEqual(bad["classification"], "publisher_face_not_on_ray")


if __name__ == "__main__":
    unittest.main()
