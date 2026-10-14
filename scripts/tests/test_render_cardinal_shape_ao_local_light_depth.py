"""Pin shape-only cardinal depth recovery in AO and local-light consumers."""

import unittest
from pathlib import Path

from shader_contract_helpers import compact, extract_function

ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / "engine/render/src/shaders"


def assert_ao_contract(source):
    recovery = compact(extract_function(source, "singleCanvasPixelToWorld3D"))
    if "rawDepth = cardinalShapeReceiverDepth(" not in recovery:
        raise AssertionError("AO reconstruction bypasses cardinal shape depth recovery")
    owner_gate = (
        "shapeOwned," if "device const uint *receiverOwners" in source else
        "finiteCoverage && shapeSampleIsOwned("
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
        "finiteCoverage && shapeSampleIsOwned("
    )
    if owner_gate not in body:
        raise AssertionError("local-light reconstruction does not preserve voxel-owned depth")
    if body.count("pixel, localLightDepth,") != 1:
        raise AssertionError("only the cardinal local-light position may use recovered depth")
    if "residualYaw == 0.0" not in body:
        raise AssertionError("local-light recovery is not restricted to cardinal yaw")


class CardinalShapeAOLocalLightDepthTest(unittest.TestCase):
    def assert_contract_rejects_mutation(self, source, contract, before, after):
        mutant = source.replace(before, after, 1)
        self.assertNotEqual(mutant, source)
        with self.assertRaises(AssertionError):
            contract(mutant)

    def test_ao_center_neighbour_and_beyond_samples_recover_shape_depth(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            source = (SHADERS / folder / f"c_compute_voxel_ao_body.{suffix}").read_text()
            with self.subTest(backend=suffix, variant="production"):
                assert_ao_contract(source)
            with self.subTest(backend=suffix, variant="recovery-removed"):
                self.assert_contract_rejects_mutation(
                    source,
                    assert_ao_contract,
                    "rawDepth = cardinalShapeReceiverDepth(",
                    "rawDepth = rawDepth + (",
                )

    def test_local_light_uses_shape_only_recovery(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            source = (SHADERS / folder / f"c_lighting_to_trixel_body.{suffix}").read_text()
            with self.subTest(backend=suffix, variant="production"):
                assert_local_light_contract(source, suffix == "metal")
            with self.subTest(backend=suffix, variant="recovery-removed"):
                self.assert_contract_rejects_mutation(
                    source,
                    lambda mutant: assert_local_light_contract(mutant, suffix == "metal"),
                    "localLightDepth,",
                    "rawDepth,",
                )


if __name__ == "__main__":
    unittest.main()
