#include <gtest/gtest.h>
#include <irreden/ir_math.hpp>

// Tests for the continuous-yaw + per-face deformation math helpers added
// in T-292 (`pos3DtoPos2DIsoYawed`, `faceDeformationMatrix`,
// `deformedTrixelIsoPixel`, `sqtToMat4`, `matrixApplyToVoxelGrid`).
//
// Acceptance (1) per #955 — round-trip test computes deformation on CPU
// and GPU for all 4 cardinals + 8 mid-sector residual yaws; asserts
// equality. The GLSL/Metal mirrors are required to use line-for-line
// identical algebra; this test exercises the CPU side against analytical
// values and pins the invariants the shader side must reproduce.

namespace {

constexpr float kTolerance = 1e-5f;

constexpr float kCardinalYaws[4] = {0.0f, IRMath::kHalfPi, IRMath::kPi, 3.0f * IRMath::kHalfPi};

// Eight residual yaws spanning the [-pi/4, pi/4] residual range, including
// the boundary, mid-sector, and a couple of small-angle samples.
constexpr float kResidualYaws[8] = {
    -IRMath::kPi / 4.0f,
    -IRMath::kPi / 6.0f,
    -IRMath::kPi / 8.0f,
    -IRMath::kPi / 16.0f,
    IRMath::kPi / 16.0f,
    IRMath::kPi / 8.0f,
    IRMath::kPi / 6.0f,
    IRMath::kPi / 4.0f,
};

void expectMat2Near(const IRMath::mat2 &actual, const IRMath::mat2 &expected, float tol) {
    EXPECT_NEAR(actual[0][0], expected[0][0], tol);
    EXPECT_NEAR(actual[0][1], expected[0][1], tol);
    EXPECT_NEAR(actual[1][0], expected[1][0], tol);
    EXPECT_NEAR(actual[1][1], expected[1][1], tol);
}

void expectIvec2Equal(IRMath::ivec2 actual, IRMath::ivec2 expected) {
    EXPECT_EQ(actual.x, expected.x);
    EXPECT_EQ(actual.y, expected.y);
}

// ---------------------------------------------------------------------------
// faceDeformationMatrix
// ---------------------------------------------------------------------------

TEST(FaceDeformationMatrixTest, IdentityAtZeroResidual) {
    const IRMath::mat2 identity(1.0f, 0.0f, 0.0f, 1.0f);
    expectMat2Near(IRMath::faceDeformationMatrix(IRMath::kXFace, 0.0f), identity, kTolerance);
    expectMat2Near(IRMath::faceDeformationMatrix(IRMath::kYFace, 0.0f), identity, kTolerance);
    expectMat2Near(IRMath::faceDeformationMatrix(IRMath::kZFace, 0.0f), identity, kTolerance);
}

TEST(FaceDeformationMatrixTest, XFaceMatchesAnalyticalForm) {
    for (float phi : kResidualYaws) {
        const float c = IRMath::cos(phi);
        const float s = IRMath::sin(phi);
        const IRMath::mat2 expected(c - s, 1.0f - (c + s), 0.0f, 1.0f);
        expectMat2Near(IRMath::faceDeformationMatrix(IRMath::kXFace, phi), expected, kTolerance);
    }
}

TEST(FaceDeformationMatrixTest, YFaceMatchesAnalyticalForm) {
    for (float phi : kResidualYaws) {
        const float c = IRMath::cos(phi);
        const float s = IRMath::sin(phi);
        const IRMath::mat2 expected(c + s, c - s - 1.0f, 0.0f, 1.0f);
        expectMat2Near(IRMath::faceDeformationMatrix(IRMath::kYFace, phi), expected, kTolerance);
    }
}

TEST(FaceDeformationMatrixTest, ZFaceIsRotationByMinusPhi) {
    for (float phi : kResidualYaws) {
        const float c = IRMath::cos(phi);
        const float s = IRMath::sin(phi);
        // 2D rotation by -phi: [c, s; -s, c]. glm::mat2 column ctor:
        // (col0.x, col0.y, col1.x, col1.y).
        const IRMath::mat2 expected(c, -s, s, c);
        expectMat2Near(IRMath::faceDeformationMatrix(IRMath::kZFace, phi), expected, kTolerance);
    }
}

TEST(FaceDeformationMatrixTest, OutOfRangeFaceReturnsIdentity) {
    const IRMath::mat2 identity(1.0f, 0.0f, 0.0f, 1.0f);
    expectMat2Near(IRMath::faceDeformationMatrix(-1, IRMath::kPi / 5.0f), identity, kTolerance);
    expectMat2Near(IRMath::faceDeformationMatrix(99, IRMath::kPi / 5.0f), identity, kTolerance);
}

// ---------------------------------------------------------------------------
// faceDeformationMatrixSO3 (T-295)
// ---------------------------------------------------------------------------

namespace {
// Pure-Z quaternion for angle theta (engine layout vec4(qx,qy,qz,qw)).
IRMath::vec4 quatRotateZ(float theta) {
    return IRMath::vec4(0.0f, 0.0f, IRMath::sin(theta * 0.5f), IRMath::cos(theta * 0.5f));
}
} // namespace

TEST(FaceDeformationMatrixSO3Test, IdentityRotationIsIdentity) {
    const IRMath::mat2 identity(1.0f, 0.0f, 0.0f, 1.0f);
    const IRMath::vec4 identityQuat(0.0f, 0.0f, 0.0f, 1.0f);
    expectMat2Near(IRMath::faceDeformationMatrixSO3(IRMath::kXFace, identityQuat), identity, 1e-4f);
    expectMat2Near(IRMath::faceDeformationMatrixSO3(IRMath::kYFace, identityQuat), identity, 1e-4f);
    expectMat2Near(IRMath::faceDeformationMatrixSO3(IRMath::kZFace, identityQuat), identity, 1e-4f);
}

// A pure-Z entity rotation must reduce to the Z-only helper. The sign flips:
// the camera-residual yaw of faceDeformationMatrix is opposite an entity
// rotating its own frame, so SO3(theta) == faceDeformationMatrix(-theta).
TEST(FaceDeformationMatrixSO3Test, PureZQuatMatchesZOnlyHelper) {
    const int faces[3] = {IRMath::kXFace, IRMath::kYFace, IRMath::kZFace};
    for (float theta : kResidualYaws) {
        const IRMath::vec4 quat = quatRotateZ(theta);
        for (int face : faces) {
            expectMat2Near(
                IRMath::faceDeformationMatrixSO3(face, quat),
                IRMath::faceDeformationMatrix(face, -theta),
                1e-4f
            );
        }
    }
}

TEST(FaceDeformationMatrixSO3Test, OutOfRangeFaceReturnsIdentity) {
    const IRMath::mat2 identity(1.0f, 0.0f, 0.0f, 1.0f);
    const IRMath::vec4 quat = quatRotateZ(IRMath::kPi / 5.0f);
    expectMat2Near(IRMath::faceDeformationMatrixSO3(-1, quat), identity, 1e-4f);
    expectMat2Near(IRMath::faceDeformationMatrixSO3(99, quat), identity, 1e-4f);
}

// ---------------------------------------------------------------------------
// octahedralSnapResidual (T-295)
// ---------------------------------------------------------------------------

namespace {
void expectQuatNearIdentity(const IRMath::vec4 &q, float tol) {
    // q and -q are the same rotation; identity is (0,0,0,±1).
    EXPECT_NEAR(IRMath::abs(q.w), 1.0f, tol);
    EXPECT_NEAR(q.x, 0.0f, tol);
    EXPECT_NEAR(q.y, 0.0f, tol);
    EXPECT_NEAR(q.z, 0.0f, tol);
}
} // namespace

TEST(OctahedralSnapResidualTest, IdentityResidualIsIdentity) {
    expectQuatNearIdentity(IRMath::octahedralSnapResidual(IRMath::vec4(0, 0, 0, 1)), 1e-4f);
}

// Every one of the 24 cube orientations snaps to itself — residual identity.
TEST(OctahedralSnapResidualTest, OctahedralElementsResidualIsIdentity) {
    const IRMath::vec4 elements[]{
        IRMath::quatAxisAngle(IRMath::vec3(1, 0, 0), IRMath::kHalfPi),        // 90 about X
        IRMath::quatAxisAngle(IRMath::vec3(0, 1, 0), IRMath::kPi),            // 180 about Y
        IRMath::quatAxisAngle(IRMath::vec3(0, 0, 1), 3.0f * IRMath::kHalfPi), // 270 about Z
        IRMath::quatAxisAngle(IRMath::vec3(1, 1, 1), IRMath::kTwoPi / 3.0f),  // 120 about diagonal
        IRMath::quatAxisAngle(IRMath::vec3(1, 1, 0), IRMath::kPi),            // 180 about edge
    };
    for (const IRMath::vec4 &e : elements) {
        expectQuatNearIdentity(IRMath::octahedralSnapResidual(e), 1e-4f);
    }
}

// A rotation smaller than the nearest snap boundary keeps identity as its
// snap, so the residual is the input unchanged.
TEST(OctahedralSnapResidualTest, SmallRotationPassesThrough) {
    const IRMath::vec4 small = IRMath::quatAxisAngle(IRMath::vec3(1, 0, 0), IRMath::kPi / 15.0f);
    const IRMath::vec4 residual = IRMath::octahedralSnapResidual(small);
    EXPECT_NEAR(residual.x, small.x, 1e-4f);
    EXPECT_NEAR(residual.y, small.y, 1e-4f);
    EXPECT_NEAR(residual.z, small.z, 1e-4f);
    EXPECT_NEAR(residual.w, small.w, 1e-4f);
}

// A near-180 rotation snaps to the 180 octahedral element, shrinking the
// residual to the small leftover — the property T-295 relies on to keep the
// per-face deformation in its clean range.
TEST(OctahedralSnapResidualTest, NearOctahedralRotationShrinksResidual) {
    const IRMath::vec4 near180 =
        IRMath::quatAxisAngle(IRMath::vec3(1, 0, 0), IRMath::kPi - IRMath::kPi / 18.0f); // 170 deg
    const IRMath::vec4 residual = IRMath::octahedralSnapResidual(near180);
    // Residual half-angle ~5 deg → w ~cos(5 deg) ~0.996; far tighter than the
    // 170 deg input (w ~0.087).
    EXPECT_GT(IRMath::abs(residual.w), 0.99f);
}

// ---------------------------------------------------------------------------
// pos3DtoPos2DIsoYawed
// ---------------------------------------------------------------------------

TEST(Pos3DtoPos2DIsoYawedTest, ZeroYawMatchesUnyawedProjection) {
    const IRMath::vec3 samples[] = {
        IRMath::vec3(0, 0, 0),
        IRMath::vec3(1, 0, 0),
        IRMath::vec3(0, 1, 0),
        IRMath::vec3(0, 0, 1),
        IRMath::vec3(3, -2, 4),
        IRMath::vec3(-5, 7, -3)
    };
    for (const auto &p : samples) {
        const IRMath::vec2 expected = IRMath::pos3DtoPos2DIso(p);
        const IRMath::vec2 actual = IRMath::pos3DtoPos2DIsoYawed(p, 0.0f);
        EXPECT_NEAR(actual.x, expected.x, kTolerance);
        EXPECT_NEAR(actual.y, expected.y, kTolerance);
    }
}

TEST(Pos3DtoPos2DIsoYawedTest, MatchesManualRzMinusYawProjection) {
    const IRMath::vec3 p(2.0f, -1.0f, 3.0f);
    for (float yaw : kCardinalYaws) {
        const float c = IRMath::cos(yaw);
        const float s = IRMath::sin(yaw);
        // viewPos = R_z(-yaw) * worldPos.
        const IRMath::vec3 viewPos(p.x * c + p.y * s, -p.x * s + p.y * c, p.z);
        const IRMath::vec2 expected = IRMath::pos3DtoPos2DIso(viewPos);
        const IRMath::vec2 actual = IRMath::pos3DtoPos2DIsoYawed(p, yaw);
        EXPECT_NEAR(actual.x, expected.x, kTolerance);
        EXPECT_NEAR(actual.y, expected.y, kTolerance);
    }
}

TEST(Pos3DtoPos2DIsoYawedTest, CardinalsAgreeWithRotateCardinalZ) {
    // At cardinal yaws, the yawed iso projection must agree with the
    // cardinal-snap path (rotateCardinalZ + pos3DtoPos2DIso) the voxel
    // rasterizer uses today. Tolerance kept tight; only sin/cos ULP drift
    // separates the two paths at cardinal yaws.
    const IRMath::vec3 p(4.0f, 2.0f, -1.0f);
    for (float yaw : kCardinalYaws) {
        const IRMath::CardinalIndex idx = IRMath::rasterYawCardinalIndex(yaw);
        const IRMath::vec3 viewPos = IRMath::rotateCardinalZ(p, idx);
        const IRMath::vec2 expected = IRMath::pos3DtoPos2DIso(viewPos);
        const IRMath::vec2 actual = IRMath::pos3DtoPos2DIsoYawed(p, yaw);
        EXPECT_NEAR(actual.x, expected.x, kTolerance);
        EXPECT_NEAR(actual.y, expected.y, kTolerance);
    }
}

// ---------------------------------------------------------------------------
// deformedTrixelIsoPixel
// ---------------------------------------------------------------------------

TEST(DeformedTrixelIsoPixelTest, ZeroResidualMatchesUnyawedOffsets) {
    // At residualYaw == 0, D = identity and the deformed offset equals the
    // shader's faceOffset_2x3:
    //   kXFace -> (1, 1+sub), kYFace -> (0, 1+sub), kZFace -> (sub, 0).
    for (int sub = 0; sub < 2; ++sub) {
        expectIvec2Equal(
            IRMath::deformedTrixelIsoPixel(IRMath::kXFace, sub, 0.0f),
            IRMath::ivec2(1, 1 + sub)
        );
        expectIvec2Equal(
            IRMath::deformedTrixelIsoPixel(IRMath::kYFace, sub, 0.0f),
            IRMath::ivec2(0, 1 + sub)
        );
        expectIvec2Equal(
            IRMath::deformedTrixelIsoPixel(IRMath::kZFace, sub, 0.0f),
            IRMath::ivec2(sub, 0)
        );
    }
}

TEST(DeformedTrixelIsoPixelTest, MatchesDirectMatrixApplication) {
    // Recompute the same value by applying faceDeformationMatrix to the
    // un-yawed offset and round-half-up'ing. The helper is meant to be
    // exactly this composition; if it ever drifts, the consumer T-293
    // shaders will desync.
    for (float phi : kResidualYaws) {
        for (int face = IRMath::kXFace; face <= IRMath::kZFace; ++face) {
            for (int sub = 0; sub < 2; ++sub) {
                IRMath::ivec2 unyawed;
                if (face == IRMath::kXFace) {
                    unyawed = IRMath::ivec2(1, 1 + sub);
                } else if (face == IRMath::kYFace) {
                    unyawed = IRMath::ivec2(0, 1 + sub);
                } else {
                    unyawed = IRMath::ivec2(sub, 0);
                }
                const IRMath::mat2 D = IRMath::faceDeformationMatrix(face, phi);
                const IRMath::vec2 d = D * IRMath::vec2(unyawed);
                const IRMath::ivec2 expected(IRMath::roundHalfUp(d.x), IRMath::roundHalfUp(d.y));
                expectIvec2Equal(IRMath::deformedTrixelIsoPixel(face, sub, phi), expected);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// sqtToMat4
// ---------------------------------------------------------------------------

TEST(SqtToMat4Test, IdentitySQTReturnsIdentity) {
    const IRMath::mat4 M = IRMath::sqtToMat4(
        IRMath::vec3(1.0f),
        IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f),
        IRMath::vec3(0.0f)
    );
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            const float expected = (col == row) ? 1.0f : 0.0f;
            EXPECT_NEAR(M[col][row], expected, kTolerance) << "col=" << col << " row=" << row;
        }
    }
}

TEST(SqtToMat4Test, PureTranslationIsoMatrixForm) {
    const IRMath::vec3 t(2.0f, -3.0f, 5.0f);
    const IRMath::mat4 M = IRMath::sqtToMat4(IRMath::vec3(1.0f), IRMath::vec4(0, 0, 0, 1), t);
    EXPECT_NEAR(M[3][0], t.x, kTolerance);
    EXPECT_NEAR(M[3][1], t.y, kTolerance);
    EXPECT_NEAR(M[3][2], t.z, kTolerance);
    EXPECT_NEAR(M[3][3], 1.0f, kTolerance);
}

TEST(SqtToMat4Test, PureScaleMatrixHasScaledColumns) {
    const IRMath::vec3 s(2.0f, 3.0f, 4.0f);
    const IRMath::mat4 M = IRMath::sqtToMat4(s, IRMath::vec4(0, 0, 0, 1), IRMath::vec3(0.0f));
    EXPECT_NEAR(M[0][0], s.x, kTolerance);
    EXPECT_NEAR(M[1][1], s.y, kTolerance);
    EXPECT_NEAR(M[2][2], s.z, kTolerance);
    // Off-diagonals on the 3x3 stay zero.
    EXPECT_NEAR(M[0][1], 0.0f, kTolerance);
    EXPECT_NEAR(M[0][2], 0.0f, kTolerance);
    EXPECT_NEAR(M[1][0], 0.0f, kTolerance);
    EXPECT_NEAR(M[1][2], 0.0f, kTolerance);
    EXPECT_NEAR(M[2][0], 0.0f, kTolerance);
    EXPECT_NEAR(M[2][1], 0.0f, kTolerance);
}

TEST(SqtToMat4Test, RotationAgreesWithRotateVectorByQuat) {
    // Build a quaternion for rotation by pi/3 around the (1, 1, 1)/sqrt(3)
    // axis. Apply M to the world basis and check against the per-vector
    // rotateVectorByQuat result.
    const float axisLen = IRMath::sqrt(3.0f);
    const IRMath::vec3 axis(1.0f / axisLen, 1.0f / axisLen, 1.0f / axisLen);
    const float angle = IRMath::kPi / 3.0f;
    const float halfAngle = angle * 0.5f;
    const float sHalf = IRMath::sin(halfAngle);
    const IRMath::vec4 q(axis.x * sHalf, axis.y * sHalf, axis.z * sHalf, IRMath::cos(halfAngle));

    const IRMath::mat4 M = IRMath::sqtToMat4(IRMath::vec3(1.0f), q, IRMath::vec3(0.0f));

    const IRMath::vec3 bases[3] =
        {IRMath::vec3(1, 0, 0), IRMath::vec3(0, 1, 0), IRMath::vec3(0, 0, 1)};
    for (int i = 0; i < 3; ++i) {
        const IRMath::vec3 expected = IRMath::rotateVectorByQuat(bases[i], q);
        const IRMath::vec4 applied = M * IRMath::vec4(bases[i], 1.0f);
        EXPECT_NEAR(applied.x, expected.x, kTolerance);
        EXPECT_NEAR(applied.y, expected.y, kTolerance);
        EXPECT_NEAR(applied.z, expected.z, kTolerance);
    }
}

TEST(SqtToMat4Test, ComposesScaleRotationTranslationInTRSOrder) {
    // Build q for 90 deg around Z: (0, 0, sin(pi/4), cos(pi/4)).
    const float halfAngle = IRMath::kHalfPi * 0.5f;
    const IRMath::vec4 q(0.0f, 0.0f, IRMath::sin(halfAngle), IRMath::cos(halfAngle));
    const IRMath::vec3 s(2.0f, 3.0f, 4.0f);
    const IRMath::vec3 t(10.0f, 20.0f, 30.0f);
    const IRMath::mat4 M = IRMath::sqtToMat4(s, q, t);

    // Local (1, 0, 0) -> scale to (2, 0, 0) -> rotate to (0, 2, 0) -> translate to (10, 22, 30).
    const IRMath::vec4 applied = M * IRMath::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(applied.x, 10.0f, kTolerance);
    EXPECT_NEAR(applied.y, 22.0f, kTolerance);
    EXPECT_NEAR(applied.z, 30.0f, kTolerance);
}

// ---------------------------------------------------------------------------
// matrixApplyToVoxelGrid
// ---------------------------------------------------------------------------

TEST(MatrixApplyToVoxelGridTest, IdentityLeavesCellsUnchanged) {
    const IRMath::mat4 I(1.0f);
    const IRMath::ivec3 cells[] = {
        IRMath::ivec3(0, 0, 0),
        IRMath::ivec3(1, 2, 3),
        IRMath::ivec3(-5, 7, -2),
        IRMath::ivec3(127, -128, 64)
    };
    for (const auto &c : cells) {
        const IRMath::ivec3 r = IRMath::matrixApplyToVoxelGrid(I, c);
        EXPECT_EQ(r.x, c.x);
        EXPECT_EQ(r.y, c.y);
        EXPECT_EQ(r.z, c.z);
    }
}

TEST(MatrixApplyToVoxelGridTest, PureTranslationShiftsByVector) {
    const IRMath::vec3 t(10.0f, -3.0f, 7.0f);
    const IRMath::mat4 M = IRMath::sqtToMat4(IRMath::vec3(1.0f), IRMath::vec4(0, 0, 0, 1), t);
    const IRMath::ivec3 cell(1, 2, 3);
    const IRMath::ivec3 r = IRMath::matrixApplyToVoxelGrid(M, cell);
    EXPECT_EQ(r.x, 11);
    EXPECT_EQ(r.y, -1);
    EXPECT_EQ(r.z, 10);
}

TEST(MatrixApplyToVoxelGridTest, Rotation90DegAroundZ) {
    // Quaternion for 90 deg around Z; applied to (1, 0, 0) gives (0, 1, 0).
    const float halfAngle = IRMath::kHalfPi * 0.5f;
    const IRMath::vec4 q(0.0f, 0.0f, IRMath::sin(halfAngle), IRMath::cos(halfAngle));
    const IRMath::mat4 M = IRMath::sqtToMat4(IRMath::vec3(1.0f), q, IRMath::vec3(0.0f));
    const IRMath::ivec3 r = IRMath::matrixApplyToVoxelGrid(M, IRMath::ivec3(1, 0, 0));
    EXPECT_EQ(r.x, 0);
    EXPECT_EQ(r.y, 1);
    EXPECT_EQ(r.z, 0);
}

TEST(MatrixApplyToVoxelGridTest, RoundsHalfIntegerUp) {
    // Quaternion for 60 deg around Z. Applied to (1, 0, 0) gives
    // (0.5, sin(60deg), 0) — verify the x component rounds to 1 (half-up).
    const float halfAngle = (IRMath::kPi / 3.0f) * 0.5f;
    const IRMath::vec4 q(0.0f, 0.0f, IRMath::sin(halfAngle), IRMath::cos(halfAngle));
    const IRMath::mat4 M = IRMath::sqtToMat4(IRMath::vec3(1.0f), q, IRMath::vec3(0.0f));
    const IRMath::ivec3 r = IRMath::matrixApplyToVoxelGrid(M, IRMath::ivec3(1, 0, 0));
    EXPECT_EQ(r.x, 1);
    EXPECT_EQ(r.y, 1);
    EXPECT_EQ(r.z, 0);
}

// ---------------------------------------------------------------------------
// isoDepthAxisModel / isoDepthAlongAxis (#1462 — detached SO(3) occlusion depth)
// ---------------------------------------------------------------------------

// Identity entity → the model-frame iso depth axis is exactly (1,1,1) with no
// FP drift (rotateVectorByQuat by the identity quat is the exact identity), so
// isoDepthAlongAxis collapses to pos3DtoDistance and the detached path stays
// byte-identical to master / the GRID world canvas.
TEST(IsoDepthAxisModelTest, IdentityIsExactlyOnes) {
    const IRMath::vec3 axis = IRMath::isoDepthAxisModel(IRMath::vec4(0, 0, 0, 1));
    EXPECT_EQ(axis.x, 1.0f);
    EXPECT_EQ(axis.y, 1.0f);
    EXPECT_EQ(axis.z, 1.0f);
}

// A 180-deg entity rotation about Z negates the in-plane (x,y) components of
// R⁻¹·(1,1,1) and leaves z — the depth order flips in x/y, exactly the
// occlusion the fixed (1,1,1) got wrong for the camera-facing +X/+Y faces.
TEST(IsoDepthAxisModelTest, Rotation180ZNegatesXY) {
    const IRMath::vec4 q = IRMath::quatAxisAngle(IRMath::vec3(0, 0, 1), IRMath::kPi);
    const IRMath::vec3 axis = IRMath::isoDepthAxisModel(q);
    EXPECT_NEAR(axis.x, -1.0f, 1e-5f);
    EXPECT_NEAR(axis.y, -1.0f, 1e-5f);
    EXPECT_NEAR(axis.z, 1.0f, 1e-5f);
}

// The depth axis is the exact vector visibleTriplet selects faces from, so the
// per-axis sign of the axis agrees with the visible face polarity — face
// visibility and occlusion order share one per-entity frame by construction.
TEST(IsoDepthAxisModelTest, SignsAgreeWithVisibleTriplet) {
    const IRMath::vec4 rots[] = {
        IRMath::vec4(0, 0, 0, 1),
        IRMath::quatAxisAngle(IRMath::vec3(0, 0, 1), IRMath::kPi),
        IRMath::quatAxisAngle(IRMath::vec3(1, 0, 0), IRMath::kHalfPi),
        IRMath::quatAxisAngle(IRMath::vec3(1, 1, 1), IRMath::kTwoPi / 3.0f),
        IRMath::quatAxisAngle(IRMath::vec3(0, 1, 0), IRMath::kPi / 3.0f),
    };
    for (const IRMath::vec4 &q : rots) {
        const IRMath::vec3 axis = IRMath::isoDepthAxisModel(q);
        const std::array<IRMath::FaceId, 3> faces = IRMath::visibleTriplet(q);
        EXPECT_EQ(axis.x < 0.0f, IRMath::faceIsPositive(faces[0]));
        EXPECT_EQ(axis.y < 0.0f, IRMath::faceIsPositive(faces[1]));
        EXPECT_EQ(axis.z < 0.0f, IRMath::faceIsPositive(faces[2]));
    }
}

// At the (1,1,1) axis the projection is exactly pos3DtoDistance (x+y+z) for
// integer positions — the world/GRID byte-identical invariant.
TEST(IsoDepthAlongAxisTest, OnesAxisMatchesPos3DtoDistance) {
    const IRMath::ivec3 samples[] =
        {{0, 0, 0}, {3, -2, 5}, {-7, 4, -1}, {10, 10, 10}, {-4, -4, -4}};
    const IRMath::vec3 ones(1.0f, 1.0f, 1.0f);
    for (const IRMath::ivec3 &p : samples) {
        EXPECT_EQ(IRMath::isoDepthAlongAxis(p, ones), IRMath::pos3DtoDistance(p));
    }
}

// The composition the renderer relies on for the identity-detached
// byte-identical invariant: isoDepthAlongAxis(p, isoDepthAxisModel(identity))
// reproduces pos3DtoDistance exactly.
TEST(IsoDepthAlongAxisTest, IdentityEntityIsBitIdenticalToPos3DtoDistance) {
    const IRMath::vec3 axis = IRMath::isoDepthAxisModel(IRMath::vec4(0, 0, 0, 1));
    const IRMath::ivec3 samples[] = {{0, 0, 0}, {3, -2, 5}, {-7, 4, -1}, {10, 10, 10}};
    for (const IRMath::ivec3 &p : samples) {
        EXPECT_EQ(IRMath::isoDepthAlongAxis(p, axis), IRMath::pos3DtoDistance(p));
    }
}

// roundHalfUp parity: a projection landing on a half-integer rounds UP, matching
// the GLSL/Metal isoDepthAlongAxis mirror (floor(v + 0.5)).
TEST(IsoDepthAlongAxisTest, RoundsHalfUpLikeShaderMirror) {
    const IRMath::vec3 axis(0.5f, 0.0f, 0.0f);
    EXPECT_EQ(IRMath::isoDepthAlongAxis(IRMath::ivec3(1, 0, 0), axis), 1);  // 0.5 → 1
    EXPECT_EQ(IRMath::isoDepthAlongAxis(IRMath::ivec3(3, 0, 0), axis), 2);  // 1.5 → 2
    EXPECT_EQ(IRMath::isoDepthAlongAxis(IRMath::ivec3(-1, 0, 0), axis), 0); // -0.5 → 0
}

// The fix in one assertion: a 180-deg-Z detached entity orders voxels along
// (-1,-1,1), the opposite in-plane sense from the fixed (1,1,1). A voxel "in
// front" under the old metric is "behind" under the rotated one — the occlusion
// swap the pitch/roll-reveals bug (#1462) was missing.
TEST(IsoDepthAlongAxisTest, Rotation180ZFlipsInPlaneOrder) {
    const IRMath::vec3 axis =
        IRMath::isoDepthAxisModel(IRMath::quatAxisAngle(IRMath::vec3(0, 0, 1), IRMath::kPi));
    const IRMath::ivec3 front(5, 5, 0); // high x+y → "far" under (1,1,1)
    const IRMath::ivec3 back(-5, -5, 0);
    EXPECT_GT(IRMath::pos3DtoDistance(front), IRMath::pos3DtoDistance(back)); // old metric
    EXPECT_LT(
        IRMath::isoDepthAlongAxis(front, axis),
        IRMath::isoDepthAlongAxis(back, axis)
    ); // rotated metric reverses the in-plane order
}

} // namespace
