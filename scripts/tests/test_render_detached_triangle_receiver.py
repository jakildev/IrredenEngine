"""Connect detached triangle publication, receiver positions and finite casters.

Literal triangle vertices and an independent rotation matrix supply the surface
oracle. This scalar adapter does not establish native owner/frame uploads or
candidate-index completeness; those require the matching engine captures.
"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

import test_render_cardinal_voxel_receiver as card
from test_render_per_axis_caster_position import cpp, functions

COMPILER = shutil.which("c++")

EXTRA = r"""

float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
vec3 cross(vec3 a, vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
vec3 &operator+=(vec3 &a, vec3 b) {
  a = a + b;
  return a;
}
vec3 &operator-=(vec3 &a, vec3 b) {
  a = a - b;
  return a;
}
vec3 &operator/=(vec3 &a, float b) {
  a = a / b;
  return a;
}
struct {
  int w = 2;
} visibleFaceIds;
vec4 detachedViewToWorld{{0, 0, 0}, 1}, detachedWorldReceive{{0, 0, 0}, 1};
float rasterYaw = 0;
"""
CHECKS = r"""

// Independent quaternion matrix, not rotateByQuat.
vec3 oracleRotate(vec3 p, vec4 q) {
  double x = q.xyz.x, y = q.xyz.y, z = q.xyz.z, w = q.w;
  return {float((1 - 2 * y * y - 2 * z * z) * p.x + (2 * x * y - 2 * w * z) * p.y +
                (2 * x * z + 2 * w * y) * p.z),
          float((2 * x * y + 2 * w * z) * p.x + (1 - 2 * x * x - 2 * z * z) * p.y +
                (2 * y * z - 2 * w * x) * p.z),
          float((2 * x * z - 2 * w * y) * p.x + (2 * y * z + 2 * w * x) * p.y +
                (1 - 2 * x * x - 2 * y * y) * p.z)};
}
vec3 unit(vec3 p) { return p / std::sqrt(dot(p, p)); }
int main(int argc, char **argv) {
  const int mutation = argc > 1 ? std::atoi(argv[1]) : 0;
  unsigned checked = 0, rays = 0;
  const vec3 points[] = {
      {0, 0, 0}, {-4, 3, -2}, {2.5f, -3.5f, .5f}, {-.5f, .25f, -.75f}, {.3f, -.2f, .49f}};
  // Each row lists triangle vertices in the two in-plane coordinates. A
  // centroid is the mean of these literal vertices, not a depth inversion.
  const int triangles[2][3][2] = {{{0, 0}, {1, 0}, {1, 1}}, {{0, 0}, {1, 1}, {0, 1}}};
  for (int mode : {0, 1})
    for (int density : {1, 2, 3, 4, 8})
      for (vec3 p : points)
        for (int slotAxis = 0; slotAxis < 3; ++slotAxis)
          for (int phase : {0, 1})
            for (int rotation = 0; rotation < 6; ++rotation) {
              voxelRenderOptions = {mode, density};
              cardinalIndex = 0;
              slot = slotAxis;
              faceId = 2 * slotAxis;
              riserFlip = 0;
              isDetachedCanvas = 1;
              dilateRevox = false;
              bothPolaritiesExposed = false;
              voxelPosition = {p, 1};
              trixelCanvasOffsetZ1 = {191 + phase, 222};
              frameCanvasOffset = {-.375f, .625f};
              const float angle = rotation < 4    ? float(rotation) * 1.5707963267948966f
                                  : rotation == 4 ? .6f
                                                  : -1.13f;
              const vec3 rotationAxis = rotation < 4 ? vec3(0, 0, 1) : unit(vec3(1, .6f, .4f));
              detachedViewToWorld = {rotationAxis * std::sin(angle / 2), std::cos(angle / 2)};
              // Includes raster cell phase transported into world space, as CPU frame setup
              // does.
              detachedWorldReceive = {
                  vec3(-17, 23, -11) +
                      oracleRotate(vec3(.25f, -.375f, .125f), detachedViewToWorld),
                  1};

              const int d = mode ? density : 1;
              for (int u = 0; u < d; ++u)
                for (int v = 0; v < d; ++v)
                  for (int half = 0; half < 2; ++half) {
                    zIdx = u * density + v;
                    const int offsets[3][2][2] = {
                        {{1, 1}, {1, 2}}, {{0, 1}, {0, 2}}, {{0, 0}, {1, 0}}};
                    localId = {offsets[slot][half][0], offsets[slot][half][1]};
                    gl_LocalInvocationID.xy = localId;
                    taps.clear();
                    produce();
                    if (taps.size() != 1)
                      return 2;
                    const vec3 actual =
                        receiveDetached(taps[0].pixel, taps[0].encoded, mutation);
                    vec3 center;
                    for (int a = 0; a < 3; ++a) {
                      double value = p[a];
                      if (mode && std::abs(value - std::round(value)) <= .0001)
                        value = std::round(value);

                      center[a] = float(std::floor(value * d + .5) / d);
                    }
                    const int expectedDepth = int(std::lround(
                        (center.x + center.y + center.z) * d)) + u + v;
                    if (decodeDepthSingle(taps[0].encoded) != expectedDepth)
                      return 6;
                    const int eu = slot == 0 ? 1 : 0, ev = slot == 2 ? 1 : 2;
                    vec3 local = center - vec3(.5f);
                    local[eu] += float(u) / d;
                    local[ev] += float(v) / d;
                    for (const auto &vertex : triangles[half]) {
                      local[eu] += float(vertex[0]) / (3 * d);
                      local[ev] += float(vertex[1]) / (3 * d);
                    }
                    const vec3 expected =
                        oracleRotate(local, detachedViewToWorld) + detachedWorldReceive.xyz;
                    if (!std::isfinite(actual.x) || !std::isfinite(actual.y) ||
                        !std::isfinite(actual.z) || std::abs(actual.x - expected.x) > 8.e-6f ||
                        std::abs(actual.y - expected.y) > 8.e-6f ||
                        std::abs(actual.z - expected.z) > 8.e-6f)
                      return fail("independent finite triangle centroid", actual, expected);
                    // The actual producer depth must be the exact integer micro-origin depth.
                    const vec3 ccenter = casterCenter(p, d);
                    const vec3 corner =
                        rotateByQuat(casterCorner(ccenter, faceId), detachedViewToWorld) +
                        detachedWorldReceive.xyz;

                    vec3 edgeU(0), edgeV(0);
                    edgeU[(slot + 1) % 3] = 1;
                    edgeV[(slot + 2) % 3] = 1;
                    edgeU = rotateByQuat(edgeU, detachedViewToWorld);
                    edgeV = rotateByQuat(edgeV, detachedViewToWorld);
                    const vec3 sun = unit(vec3(-.42f, -.60f, -.55f)),
                               basisU = unit(cross(sun, vec3(0, 0, 1))),
                               basisV = cross(sun, basisU);

                    const vec3 receiver = sunSpaceProject(actual, basisU, basisV, sun),
                               a = sunSpaceProject(corner, basisU, basisV, sun);

                    const vec3 bu = sunSpaceProject(edgeU, basisU, basisV, sun),
                               bv = sunSpaceProject(edgeV, basisU, basisV, sun);

                    const float own = sourceFaceRaySeparation({receiver.x, receiver.y},
                                                              receiver.z, a, bu, bv);
                    if (!std::isfinite(own) || std::abs(own) > 4.f / 1024.f)
                      return fail("own caster face separation", vec3(own), vec3(0));
                    // Independently specified quarter-unit occluder, no shadow for a face
                    // behind.
                    const float toward = sourceFaceRaySeparation(
                        {receiver.x, receiver.y}, receiver.z, a + vec3(0, 0, -.25f), bu, bv);

                    const float behind = sourceFaceRaySeparation(
                        {receiver.x, receiver.y}, receiver.z, a + vec3(0, 0, .25f), bu, bv);

                    const float outside = sourceFaceRaySeparation(
                        {receiver.x, receiver.y}, receiver.z, a + bu * 4.f, bu, bv);
                    if (!std::isfinite(toward) || !std::isfinite(behind) ||
                        std::abs(toward - .25f) > 4.f / 1024.f ||
                        std::abs(behind + .25f) > 4.f / 1024.f || outside != -1.f)
                      return 5;
                    ++checked;
                    rays += 4;
                  }
            }
  printf("centroids=%u finite_rays=%u\n", checked, rays);
  return 0;
}
"""


def harness(backend):
    shaders = card.shader_directory(backend)
    common = (shaders / f"ir_iso_common.{backend}").read_text()
    source, _ = card.harness(backend)
    source = source.replace(card.CHECKS, "")
    source = source.replace(
        "int isoDepthAlongAxis(ivec3,vec3){std::abort();}", "int isoDepthAlongAxis(ivec3,vec3);"
    )
    source += EXTRA
    for name in (
        "isoDepthAlongAxis",
        "isoPixelToWorld3D",
        "rasterYawCardinalIndex",
        "trixelCanvasPixelToWorld3D",
        "rotateByQuat",
        "fracToFrac4",
        "perAxisRenderedVoxelCenter",
    ):
        source += "\n" + cpp(functions(common, name))
    lighting = (shaders / f"c_lighting_to_trixel_body.{backend}").read_text()
    start = lighting.index("        worldReceivePos = trixelCanvasPixelToWorld3D(")
    end = lighting.index("        worldNormal = rotateByQuat(worldNormal,", start)
    body = cpp(lighting[start:end]).replace("voxelFrameData.", "").replace("frameData.", "")
    # Mutations target the live production expressions, one copy each.
    body = body.replace(
        "((isoRel.x + isoRel.y) & 1)", "(((isoRel.x + isoRel.y) & 1) ^ (mutation == 1 ? 1 : 0))"
    )
    body = body.replace(
        "worldReceivePos -= kVoxelRasterCellAnchor;",
        "if(mutation != 2) worldReceivePos -= kVoxelRasterCellAnchor;",
    )
    body = body.replace(
        "float(effectiveTrixelSubdivisionScale(voxelRenderOptions))",
        "float(mutation == 3 ? 1 : effectiveTrixelSubdivisionScale(voxelRenderOptions))",
    )
    source += (
        "\nvec3 receiveDetached(ivec2 pixel,int encoded,int mutation){\n"
        "int rawDepth=decodeDepthSingle(encoded);vec3 worldReceivePos;\n"
        + body
        + "\nreturn worldReceivePos;}\n"
    )
    caster = cpp((shaders / f"c_bake_voxel_sun_faces.{backend}").read_text())
    expression = re.search(
        r"const bool rigidSource = [^;]+;\s*const vec3 position = [^;]+;", caster
    )[0]
    source += (
        "\nvec3 casterCenter(vec3 p,int density){struct{int w=1;}dispatch;\n"
        "const vec4 positions[]={{p,1}};const int index=0;\n"
        "const vec4 worldOrigin{vec3(0),0};const float subdivisions=float(density);\n"
        + expression
        + "\nreturn position;}\n"
    )
    corner = re.search(r"vec3 corner = [^;]+;\s*corner\[axis\] \+= [^;]+;", caster)[0]
    source += (
        "\nvec3 casterCorner(vec3 position,int faceId){\n"
        "const int axis=faceId/2;const bool positive=(faceId&1)!=0;\n"
        + corner
        + "\nreturn corner;}\n"
    )
    for filename, names in (
        ("ir_sun_projection", ("sunSpaceProject",)),
        ("ir_projected_face", ("projectedFaceDeterminant", "projectedFaceCoordinates")),
        ("ir_sun_face_query_layout", ("sourceFaceRaySeparation",)),
    ):
        text = (shaders / f"{filename}.{backend}").read_text()
        for name in names:
            source += "\n" + cpp(functions(text, name))
    source += CHECKS
    return source


@unittest.skipUnless(COMPILER, "detached receiver controls require a C++ compiler")
class DetachedTriangleReceiverTest(unittest.TestCase):
    def test_triangle_centroids_and_finite_ray_separation(self):
        for backend in ("glsl", "metal"):
            with self.subTest(backend=backend), tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary)
                source, executable = path / "receiver.cpp", path / "receiver"
                source.write_text(harness(backend))
                build = subprocess.run(
                    [COMPILER, "-std=c++17", "-O1", str(source), "-o", str(executable)],
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(build.returncode, 0, build.stderr)
                result = subprocess.run([str(executable)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("centroids=35640 finite_rays=142560", result.stdout)
                for mutation, label in enumerate(("parity", "anchor", "density"), 1):
                    with self.subTest(mutation=label):
                        result = subprocess.run(
                            [str(executable), str(mutation)], capture_output=True, text=True
                        )
                        self.assertNotEqual(result.returncode, 0, "mutation escaped")
                        self.assertIn("independent finite triangle centroid", result.stderr)


if __name__ == "__main__":
    unittest.main()
