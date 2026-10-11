"""Pin shape-only cardinal depth recovery in AO and local-light consumers."""

import unittest
from pathlib import Path

from shader_contract_helpers import extract_function

ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / "engine/render/src/shaders"


def compact(source):
    return " ".join(source.split())


def assert_ao_contract(source):
    recovery = compact(extract_function(source, "singleCanvasPixelToWorld3D"))
    if "rawDepth = cardinalShapeReceiverDepth(" not in recovery:
        raise AssertionError("AO reconstruction bypasses cardinal shape depth recovery")
    owner_gate = (
        "shapeOwned," if "device const uint *receiverOwners" in source else
        "finiteCoverage && aoSampleIsShapeOwned("
    )
    if owner_gate not in recovery:
        raise AssertionError("AO reconstruction does not gate recovery on shape ownership")
    if "smoothMode && receiverFrame.smoothYawEnabled == 0" not in recovery:
        raise AssertionError("AO reconstruction does not gate recovery on the cardinal store")
    if source.count("singleCanvasPixelToWorld3D(") != 4:
        raise AssertionError("AO center, neighbour, and beyond samples must share recovery")


def assert_local_light_contract(source, metal):
    body = compact(source)
    if "localLightDepth = cardinalShapeReceiverDepth(" not in body:
        raise AssertionError("local-light reconstruction bypasses cardinal shape depth recovery")
    owner_gate = "shapeOwned," if metal else (
        "finiteCoverage && lightingSampleIsShapeOwned(pixel, size.x),"
    )
    if owner_gate not in body:
        raise AssertionError("local-light reconstruction does not preserve voxel-owned depth")
    if body.count("pixel, localLightDepth,") != 1:
        raise AssertionError("only the cardinal local-light position may use recovered depth")
    if "residualYaw == 0.0" not in body:
        raise AssertionError("local-light recovery is not restricted to cardinal yaw")


class CardinalShapeAOLocalLightDepthTest(unittest.TestCase):
    def test_ao_center_neighbour_and_beyond_samples_recover_shape_depth(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            source = (SHADERS / folder / f"c_compute_voxel_ao_body.{suffix}").read_text()
            with self.subTest(backend=suffix, variant="production"):
                assert_ao_contract(source)
            mutant = source.replace(
                "rawDepth = cardinalShapeReceiverDepth(",
                "rawDepth = rawDepth + (",
                1,
            )
            self.assertNotEqual(mutant, source)
            with self.subTest(backend=suffix, variant="recovery-removed"):
                with self.assertRaises(AssertionError):
                    assert_ao_contract(mutant)

    def test_local_light_uses_shape_only_recovery(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            source = (SHADERS / folder / f"c_lighting_to_trixel_body.{suffix}").read_text()
            with self.subTest(backend=suffix, variant="production"):
                assert_local_light_contract(source, suffix == "metal")
            mutant = source.replace(
                "localLightDepth,",
                "rawDepth,",
                1,
            )
            self.assertNotEqual(mutant, source)
            with self.subTest(backend=suffix, variant="recovery-removed"):
                with self.assertRaises(AssertionError):
                    assert_local_light_contract(mutant, suffix == "metal")


if __name__ == "__main__":
    unittest.main()
