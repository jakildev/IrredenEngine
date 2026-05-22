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

constexpr float kCardinalYaws[4] = {
    0.0f,
    IRMath::kHalfPi,
    IRMath::kPi,
    3.0f * IRMath::kHalfPi
};

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

void expectMat2Near(const IRMath::mat2& actual, const IRMath::mat2& expected, float tol) {
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
    for (const auto& p : samples) {
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
    const IRMath::mat4 M =
        IRMath::sqtToMat4(s, IRMath::vec4(0, 0, 0, 1), IRMath::vec3(0.0f));
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

    const IRMath::vec3 bases[3] = {
        IRMath::vec3(1, 0, 0), IRMath::vec3(0, 1, 0), IRMath::vec3(0, 0, 1)
    };
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
    for (const auto& c : cells) {
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

} // namespace
