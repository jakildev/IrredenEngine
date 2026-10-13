"""Execute retained cylinder rays and guard unsupported disconnected fields."""

import math
import random
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import replace_once
from test_render_sdf_ray_roots import field
from test_render_sphere_shadow_receiver import HARNESS, cpp, receiver_functions

COMPILER = shutil.which("c++")
DRIVER = r"""
#include <iostream>
#include <iomanip>
int main(){
 ShapeDescriptor shape; ShapeProjectionData frame;
 ivec2 sample;
 while(std::cin >> shape.shapeType >> shape.params.xyz.x >> shape.params.xyz.y
       >> shape.params.xyz.z >> shape.params.w
       >> shape.worldPosition.xyz.x >> shape.worldPosition.xyz.y >> shape.worldPosition.xyz.z
       >> shape.rotation.xyz.x >> shape.rotation.xyz.y >> shape.rotation.xyz.z >> shape.rotation.w
       >> frame.smoothYawEnabled >> frame.voxelRenderOptions.y
       >> frame.visualYaw >> frame.rasterYaw >> sample.x >> sample.y
       >> shape.flags >> frame.latticeShapes >> frame.finiteCoverage >> frame.residualYaw){
  frame.voxelRenderOptions.x=1;
  vec3 position(91); bool hit=shapeAnalyticShadowReceiver(shape,frame,sample,position);
  receiverFrame=frame; receiverShapes[2]=shape; receiverTiles[2].shapeIndex=2;
  bool ownersMatch=true;
  for(uint lane:{0u,7u,8u,35u,63u})for(uint face=0;face<3;++face)for(uint half=0;half<2;++half){
   receiverTiles[2].tileIsoOrigin=sample-ivec2(int(lane%8),int(lane/8));
   receiverOwners[3]=((2u*64u+lane)*3u+face)*2u+half;
   vec3 selected(91);
   bool selectedHit=selectedShapeAnalyticShadowReceiver(pixel,size.x,selected);
   ownersMatch &= selectedHit==hit && close(position,selected,0.f);
  }
  std::cout << hit << ' ' << std::setprecision(10) << position.x << ' '
            << position.y << ' ' << position.z << ' ' << ownersMatch << '\n';
 }
}
"""


def f32(value):
    return struct.unpack("f", struct.pack("f", value))[0]


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def unit(vector):
    length = math.sqrt(dot(vector, vector))
    return tuple(x / length for x in vector)


def rotation_matrix(quaternion):
    x, y, z, w = quaternion
    return ((1 - 2 * (y*y + z*z), 2 * (x*y - z*w), 2 * (x*z + y*w)),
            (2 * (x*y + z*w), 1 - 2 * (x*x + z*z), 2 * (y*z - x*w)),
            (2 * (x*z - y*w), 2 * (y*z + x*w), 1 - 2 * (x*x + y*y)))


def quaternion_between(source, target):
    cross = (source[1]*target[2] - source[2]*target[1],
             source[2]*target[0] - source[0]*target[2],
             source[0]*target[1] - source[1]*target[0])
    return tuple(f32(x) for x in unit((*cross, 1 + dot(source, target))))


def reference_ray(row):
    _, *rest = row
    center = rest[4:7]
    quaternion = rest[7:11]
    smooth, density, yaw, raster = rest[11:15]
    sample = rest[15:17]
    if smooth:
        c, s = math.cos(yaw), math.sin(yaw)
    else:
        quarter = math.floor(raster / (math.pi / 2) + .5) % 4
        c, s = ((1, 0), (0, 1), (-1, 0), (0, -1))[quarter]
        vx, vy = c*center[0] + s*center[1], -s*center[0] + c*center[1]
        vx, vy, z = math.floor(vx + .5), math.floor(vy + .5), math.floor(center[2] + .5)
        center = (c*vx - s*vy, s*vx + c*vy, z)
    vx, vy = c*center[0] + s*center[1], -s*center[0] + c*center[1]
    oi = math.floor((-vx + vy)*density + .5)
    oj = math.floor((-vx - vy + 2*center[2])*density + .5)
    u, v = (sample[0] - oi)/density, (sample[1] - oj)/density
    view = (-u/2 - v/6, u/2 - v/6, v/3)
    origin = (c*view[0] - s*view[1], s*view[0] + c*view[1], view[2])
    direction = ((c-s)/math.sqrt(3), (s+c)/math.sqrt(3), 1/math.sqrt(3))
    if abs(quaternion[3]) < .9999:
        columns = tuple(zip(*rotation_matrix(quaternion)))
        local_origin = tuple(dot(column, origin) for column in columns)
        local_direction = tuple(dot(column, direction) for column in columns)
    else:
        local_origin, local_direction = origin, direction
    return center, origin, direction, local_origin, local_direction


def expected_boundary(row):
    kind, *rest = row
    params = rest[:4]
    density = rest[12]
    center, origin, direction, local_origin, local_direction = reference_ray(row)
    # Field bracketing is independent of the shader polynomial/cap decomposition.
    # Reject uncertain coarse misses instead of declaring an unsampled rim absent.
    radius = math.sqrt(2*(params[0] + (params[1] if kind == 9 else 0) + .5)**2
                       + ((params[1] if kind == 9 else params[2]/2) + .5)**2)
    bound = math.sqrt(dot(local_origin, local_origin)) + radius
    threshold = .5/density

    def value(depth):
        point = tuple(p + d*depth for p, d in zip(local_origin, local_direction))
        return field(kind, params, point) - threshold

    minimum, previous = math.inf, -bound
    step = 2*bound/2048
    for index in range(2049):
        candidate = -bound + step*index
        distance = value(candidate)
        minimum = min(minimum, distance)
        if distance <= 0:
            left, right = previous, candidate
            for _ in range(48):
                middle = (left + right)/2
                if value(middle) <= 0:
                    right = middle
                else:
                    left = middle
            return tuple(c + p + d*right for c, p, d in zip(center, origin, direction))
        previous = candidate
    lipschitz = 2 + (params[0]/params[2] if kind == 8 else 0)
    return None if minimum > lipschitz*step else "uncertain"


def row(kind, params, center=(0, 0, 0), rotation=(0, 0, 0, 1),
        smooth=1, density=8, yaw=0, raster=0, sample=(0, 0),
        flags=0, lattice=0, finite=1, residual=0):
    return [kind, *(f32(x) for x in params), *(f32(x) for x in center),
            *(f32(x) for x in rotation), smooth, density, f32(yaw), f32(raster),
            *sample, flags, lattice, finite, f32(residual)]


def build(directory, suffix, mutation=None):
    source = HARNESS + cpp(receiver_functions(suffix)) + DRIVER
    if mutation:
        source = replace_once(source, *mutation)
    path = directory / f"curved-{suffix}.cpp"
    path.write_text(source)
    executable = directory / f"curved-{suffix}"
    result = subprocess.run([COMPILER, "-std=c++20", "-O2", str(path), "-o", str(executable)],
                            capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    return executable


@unittest.skipUnless(COMPILER, "curved receiver controls require a C++ compiler")
class CurvedShadowReceiverTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.executables = [(suffix, build(Path(cls.temporary.name), suffix))
                           for suffix in ("glsl", "metal")]

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def mismatches(self, executable, controls):
        inputs = "\n".join(" ".join(map(str, case)) for case, _ in controls) + "\n"
        result = subprocess.run([str(executable)], input=inputs, capture_output=True,
                                text=True, check=True)
        outputs = result.stdout.splitlines()
        self.assertEqual(len(outputs), len(controls))
        mismatches = []
        for index, ((case, expected), output) in enumerate(zip(controls, outputs)):
            hit, x, y, z, owners_match = output.split()
            position = (float(x), float(y), float(z))
            valid = (hit == "1") == (expected is not None) and owners_match == "1"
            target = expected if expected is not None else (91, 91, 91)
            valid &= math.dist(position, target) <= .001
            if not valid:
                mismatches.append((index, case, expected, output))
        return mismatches

    def check_controls(self, controls):
        for suffix, executable in self.executables:
            with self.subTest(backend=suffix):
                errors = self.mismatches(executable, controls)
                self.assertEqual(errors, [], str(errors[:4]))
                print(f"{suffix}: curved receiver rays={len(controls)} "
                      f"owner queries={30*len(controls)}")

    def test_axis_aligned_cylinder_caps(self):
        controls = []
        direction = unit((1, 1, 1))
        for density in (2, 8, 32):
            for axis in ((0, 0, 1), (0, 0, -1)):
                rotation = quaternion_between(axis, direction)
                params = (3, 0, 7, 0)
                case = row(2, params, rotation=rotation, density=density)
                depth = -params[2]/2 - .5/density
                controls.append((case, tuple(x*depth for x in direction)))
        self.check_controls(controls)

    def test_cone_threshold_field_has_separate_ray_intervals(self):
        params, threshold = (4, 0, 8, 0), .25
        first, second = (.2485, 0, 4.01), (.2535, 0, 3.99)
        middle = tuple((a+b)/2 for a, b in zip(first, second))
        self.assertLess(field(8, params, first), threshold)
        self.assertLess(field(8, params, second), threshold)
        self.assertGreater(field(8, params, middle), threshold)
        self.check_controls([(row(8, params, density=2), None)])

    def test_skipped_near_torus_component_retains_fallback(self):
        density, major, tube = 2, 4, 1.5
        threshold = .5/density
        beta = math.asin(.23335)
        tilt = math.atan(1/math.sqrt(2)) - beta
        axis = unit((1, -1, 0))
        rotation = (*[v*math.sin(tilt/2) for v in axis], math.cos(tilt/2))
        case = row(9, (major, tube, 0, 0), rotation=rotation,
                   density=density, sample=(0, -4))
        # The radial/Z section is two circles. Neither root computation nor
        # the producer's integer search is used to derive their intervals.
        offset = -4/(math.sqrt(6)*density)
        intervals = []
        for side in (-1, 1):
            center = side*math.sqrt(3)*density*major*math.cos(beta)
            radius = math.sqrt(3*density*density*((tube+threshold)**2 -
                               (offset+side*major*math.sin(beta))**2))
            intervals.append((center-radius, center+radius))
        near, far = intervals
        self.assertGreater(math.ceil(near[0]), math.floor(near[1]))
        self.assertEqual(math.ceil(far[0]), 8)
        _, _, _, origin, direction = reference_ray(case)
        accepted = [depth for depth in range(-32, 33)
                    if field(9, (major, tube, 0, 0), tuple(
                        p+d*depth/(math.sqrt(3)*density)
                        for p, d in zip(origin, direction))) <= threshold]
        self.assertEqual(accepted[0], 8)
        self.assertGreater(accepted[0]-near[1], 20)
        self.check_controls([(case, None)])
        mutation = ("shape.shapeType == SHAPE_CYLINDER;",
                    "(shape.shapeType == SHAPE_CYLINDER || shape.shapeType == SHAPE_TORUS);")
        for suffix in ("glsl", "metal"):
            with self.subTest(backend=suffix):
                directory = Path(self.temporary.name) / f"{suffix}-torus-enabled"
                directory.mkdir()
                executable = build(directory, suffix, mutation)
                self.assertTrue(self.mismatches(executable, [(case, None)]))

    def test_transforms_against_independent_field(self):
        generator = random.Random(4386)
        controls = []
        for kind, params in ((2, (3, 0, 7, 0)),):
            for index in range(256):
                density = (2, 4, 8, 16)[index % 4]
                yaw = (index % 16)*math.pi/8
                smooth = index % 3 != 0
                quaternion = unit(tuple(generator.uniform(-1, 1) for _ in range(4)))
                if index % 5 == 0:
                    quaternion = (0, 0, 0, 1)
                if index % 7 == 0:
                    quaternion = (0.005, 0, 0, math.sqrt(1 - .005**2))
                case = row(kind, params, center=tuple(generator.uniform(-3, 3) for _ in range(3)),
                           rotation=quaternion, smooth=int(smooth), density=density, yaw=yaw,
                           raster=(index % 4)*math.pi/2,
                           sample=(generator.randrange(-8*density, 8*density),
                                   generator.randrange(-8*density, 8*density)))
                expected = expected_boundary(case)
                if expected != "uncertain":
                    controls.append((case, expected))
        self.assertGreater(len(controls), 250)
        self.assertGreater(sum(expected is not None for _, expected in controls), 30)
        self.check_controls(controls)
        mutations = {
            "model_rotation_ignored": ("if (shapeHasEntityRotation(shape.rotation))", "if (false)"),
            "inverse_rotation_reversed": (
                "localOrigin = rotateByInverseQuat(worldOrigin, shape.rotation)",
                "localOrigin = rotateByQuat(worldOrigin, shape.rotation)"),
            "field_threshold_lost": ("float threshold = 0.5f / float(density)",
                                     "float threshold = 0.0f / float(density)"),
            "cylinder_unsupported": ("shape.shapeType == SHAPE_CYLINDER;", "false;"),
            "rounded_sample_interior": ("viewOffset = origin + direction * entry;\n    }\n",
                                        "viewOffset = origin + direction * floor(entry);\n    }\n"),
        }
        for suffix in ("glsl", "metal"):
            for name, mutation in mutations.items():
                with self.subTest(backend=suffix, mutation=name):
                    directory = Path(self.temporary.name) / f"{suffix}-{name}"
                    directory.mkdir()
                    executable = build(directory, suffix, mutation)
                    self.assertTrue(self.mismatches(executable, controls), name)

    def test_unsupported_representation_preserves_output(self):
        controls = []
        for kind, params in ((2, (3, 0, 7, 0)), (8, (4, 0, 8, 0)), (9, (4, 2, 0, 0))):
            for options in ({"flags": 1}, {"lattice": 1}, {"density": 1}, {"finite": 0},
                            {"smooth": 0, "residual": .1}):
                controls.append((row(kind, params, **options), None))
        controls += [(row(kind, params), None) for kind, params in
                     ((8, (4, 0, 8, 0)), (9, (4, 1.5, 0, 0)))]
        self.check_controls(controls)


if __name__ == "__main__":
    unittest.main()
