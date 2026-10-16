"""Connect a settled GRID stair's finite occlusion to the cardinal receiver.

The independent occupancy/slab oracle checks the represented unit cubes. Shader
controls execute publication, receiver reconstruction, and finite-face queries;
they do not emulate the GPU rasterizer or compare reference images.
"""

import math
import re
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_function, replace_once
from test_render_cardinal_voxel_receiver import CHECKS as GEOMETRY_CHECKS
from test_render_cardinal_voxel_receiver import harness as geometry_harness
from test_render_cardinal_voxel_receiver import shader_directory
from test_render_per_axis_caster_position import cpp

COMPILER = shutil.which("c++")
SUN = (-0.42, -0.60, -0.55)
SUN = tuple(value / math.sqrt(sum(value * value for value in SUN)) for value in SUN)
RECEIVER = (10.5, -17.125, -11.375)


def float32(value):
    return struct.unpack("f", struct.pack("f", value))[0]


def settled_grid_cells():
    """Inverse sample the centered, carved 12-cell prism after its damped turn."""
    rate, decay, angle = float32(0.2), float32(1 - float32(0.06)), 0.0
    while rate >= float32(0.0001):
        angle += rate
        rate = float32(rate * decay)
    axis = (0.3, 0.2, 1.0)
    length = math.sqrt(sum(value * value for value in axis))
    x, y, z = (value / length for value in axis)
    c, s = math.cos(angle), math.sin(angle)
    rotation = (
        (c + x*x*(1-c), x*y*(1-c) - z*s, x*z*(1-c) + y*s),
        (y*x*(1-c) + z*s, c + y*y*(1-c), y*z*(1-c) - x*s),
        (z*x*(1-c) - y*s, z*y*(1-c) + x*s, c + z*z*(1-c)),
    )
    cells = set()
    # A rotated 12-cell box fits inside this 27-cell box around its translation.
    for wx in range(5, 32):
        for wy in range(-31, -4):
            for wz in range(-25, 2):
                relative = (wx - 18, wy + 18, wz + 12)
                source = tuple(math.floor(sum(rotation[j][i] * relative[j]
                                             for j in range(3)) + 0.5) for i in range(3))
                # Authored centers [-5.5,5.5] map to source cells [-5,6].
                if (all(-5 <= value <= 6 for value in source)
                        and not (source[0] >= 1 and source[1] >= 1)):
                    cells.add((wx, wy, wz))
    return cells


def ray_box(origin, cell, direction):
    intervals = [sorted(((cell[i] - 0.5 - origin[i]) / direction[i],
                         (cell[i] + 0.5 - origin[i]) / direction[i])) for i in range(3)]
    entry = max(pair[0] for pair in intervals)
    leave = min(pair[1] for pair in intervals)
    return (entry, leave) if leave >= max(entry, 4 / 1024) else None


def blockers(origin, cells, direction=SUN):
    return {cell: interval for cell in cells
            if (interval := ray_box(origin, cell, direction)) is not None}


SHADER_CHECKS = r"""
int main(int argc,char** argv){
 const int mutation=argc>1?std::atoi(argv[1]):0;
 voxelRenderOptions={1,4};cardinalIndex=0;slot=0;faceId=0;riserFlip=0;
 voxelPosition={vec3(11,-17,-11),1};bothPolaritiesExposed=false;
 frameCanvasOffset={3.75f,-2.25f};trixelCanvasOffsetZ1=trixelOriginOffsetZ1({642,722});
 const vec3 sun=SUN_DIRECTION;
 const vec3 basisU=SUN_BASIS_U,basisV=SUN_BASIS_V;
 const vec3 casterCorner=sunSpaceProject(vec3(9.5f,-19.5f,-13.5f),basisU,basisV,sun);
 const vec3 edgeU=sunSpaceProject(vec3(0,1,0),basisU,basisV,sun);
 const vec3 edgeV=sunSpaceProject(vec3(0,0,1),basisU,basisV,sun);
 const vec3 ownCorner=sunSpaceProject(vec3(10.5f,-17.5f,-11.5f),basisU,basisV,sun);
 for(int v=0;v<4;++v)for(int half=0;half<2;++half){
  zIdx=4+v;localId={1,1+half};gl_LocalInvocationID.xy=localId;
  taps.clear();produce();if(taps.size()!=1)return 1;
  const vec3 surface=cardinalVoxelSurfaceCenter(taps[0].pixel,taps[0].encoded,
      trixelCanvasOffsetZ1,frameCanvasOffset,voxelRenderOptions,0);
  const vec3 expected(10.5f,-17.125f,-11.375f+.25f*v);
  if(!same(surface,expected))return fail("stair receiver",surface,expected);
  const vec3 projected=sunSpaceProject(surface,basisU,basisV,sun);
  const float self=sourceFaceRaySeparation({projected.x,projected.y},projected.z,
                                        ownCorner,edgeU,edgeV);
  if(std::abs(self)>4.f/1024.f){
   std::fprintf(stderr,"self plane separation: %.9g\n",self);return 2;
  }
  vec3 query=surface;
  if(mutation==1)query.x-=.5f;
  if(mutation==2)query=vec3(10.5f,-17.f,-11.f);
  const vec3 sunPoint=sunSpaceProject(query,basisU,basisV,sun);
  const auto intersect=mutation==3?unboundedSourceFaceRaySeparation:sourceFaceRaySeparation;
  const float separation=intersect({sunPoint.x,sunPoint.y},sunPoint.z, casterCorner,edgeU,edgeV);
  if(v==0){
   // The exposed -X plane is reached after entering the neighboring cube union.
   if(std::abs(separation-2.18074963f)>.00005f){
    std::fprintf(stderr,"stair blocker missing: %.9g\n",separation);return 3;
   }
  }else if(separation>=0){
   std::fprintf(stderr,"adjacent micro-row falsely shadowed\n");return 4;
  }
 }
 std::printf("microface_queries=8 self_planes=8\n");return 0;
}
"""


class ImpulseShadowTest(unittest.TestCase):
    def test_independent_grid_slab_witness_and_adjacent_rows(self):
        cells = settled_grid_cells()
        self.assertIn((11, -17, -11), cells)
        self.assertNotIn((10, -17, -11), cells)
        self.assertIn((10, -18, -13), cells)
        self.assertIn((10, -19, -13), cells)
        self.assertNotIn((9, -19, -13), cells)
        self.assertFalse(blockers(RECEIVER, cells, (-1, -1, -1)))
        hits = blockers(RECEIVER, cells)
        first = min(hits, key=lambda cell: hits[cell][0])
        self.assertEqual(first, (10, -18, -13))
        self.assertAlmostEqual(hits[first][0], 1.873462179279337)
        self.assertAlmostEqual(hits[(10, -19, -13)][1], 2.1807496266743605)
        for row in range(1, 4):
            self.assertFalse(blockers((RECEIVER[0], RECEIVER[1], RECEIVER[2] + row / 4), cells))
        shifted = (RECEIVER[0] - 0.5, RECEIVER[1], RECEIVER[2])
        self.assertFalse(blockers(shifted, cells))
        self.assertFalse(blockers((10.5, -17, -11), cells))

    @unittest.skipUnless(COMPILER, "finite stair receiver controls require a C++ compiler")
    def test_production_microface_and_finite_query(self):
        # Any orthonormal sun basis preserves ray intersections; the oracle's
        # direct world-space slabs do not use this projection.
        length = math.hypot(SUN[0], SUN[1])
        basis_u = (-SUN[1] / length, SUN[0] / length, 0)
        basis_v = (SUN[1]*basis_u[2] - SUN[2]*basis_u[1],
                   SUN[2]*basis_u[0] - SUN[0]*basis_u[2],
                   SUN[0]*basis_u[1] - SUN[1]*basis_u[0])
        checks = SHADER_CHECKS
        for token, values in (("SUN_DIRECTION", SUN), ("SUN_BASIS_U", basis_u),
                              ("SUN_BASIS_V", basis_v)):
            checks = checks.replace(token, "vec3(" + ",".join(f"{v:.9f}f" for v in values) + ")")
        for backend in ("glsl", "metal"):
            shaders = shader_directory(backend)
            source, _ = geometry_harness(backend)
            source = replace_once(source, GEOMETRY_CHECKS, "")
            helpers = """
#include <type_traits>
using std::abs;
static_assert(std::is_same_v<decltype(abs(0.5f)), float>, "shader abs must preserve float");
float dot(vec3 a,vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
"""
            for file, names in (
                ("ir_projected_face", ("projectedFaceDeterminant", "projectedFaceCoordinates",
                                       "projectedFaceContainsClosed")),
                ("ir_sun_projection", ("sunSpaceProject",)),
                ("ir_sun_face_query_layout", ("sourceFaceRaySeparation",)),
            ):
                shader = (shaders / f"{file}.{backend}").read_text()
                helpers += "\n".join(extract_function(shader, name) for name in names) + "\n"
            bounds = re.search(
                r"if \((!projectedFaceContainsClosed[\s\S]*?)\) \{", helpers
            ).group(1)
            unbounded = replace_once(extract_function(helpers, "sourceFaceRaySeparation"),
                                     bounds, "false")
            unbounded = unbounded.replace("sourceFaceRaySeparation",
                                          "unboundedSourceFaceRaySeparation")
            source += cpp(helpers + unbounded) + checks
            with self.subTest(backend=backend), tempfile.TemporaryDirectory() as directory:
                path = Path(directory)
                (path / "stair.cpp").write_text(source)
                build = subprocess.run([COMPILER, "-std=c++17", "-O2", str(path / "stair.cpp"),
                                        "-o", str(path / "stair")], capture_output=True, text=True)
                self.assertEqual(build.returncode, 0, build.stderr)
                for mutation in range(4):
                    with self.subTest(mutation=mutation):
                        run = subprocess.run([str(path / "stair"), str(mutation)],
                                             capture_output=True, text=True)
                        if mutation == 0:
                            self.assertEqual(run.returncode, 0, run.stderr)
                            print(f"{backend}: {run.stdout}", end="")
                        else:
                            self.assertEqual(run.returncode, 4 if mutation == 3 else 3, run.stderr)


if __name__ == "__main__":
    unittest.main()
