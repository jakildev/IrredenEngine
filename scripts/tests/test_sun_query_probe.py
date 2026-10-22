"""Portable controls for native probe assembly, layout, and fixture semantics.

The CPU adapter executes the finite production sampler and probe kernels; it
does not validate Metal compilation, GPU timestamps, or native fast math.
"""

import importlib.util
import io
import json
import re
import subprocess
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

import test_render_source_face_overflow as receiver
from shader_contract_helpers import extract_function

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "sun_query_probe", ROOT / "scripts/perf/sun_query_probe.py")
PROBE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROBE)


class SunQueryProbeAssemblyTest(unittest.TestCase):
    def test_specializations_embed_actual_sampler_and_recursive_helpers(self):
        timed, diagnostic = (PROBE.shader_source(mode) for mode in (False, True))
        self.assertEqual(timed.replace("#define IR_SUN_QUERY_STATS 0", "", 1),
                         diagnostic.replace("#define IR_SUN_QUERY_STATS 1", "", 1))
        self.assertNotRegex(timed, r'#include\s+"')
        folder = ROOT / "engine/render/src/shaders/metal"
        for filename, function in (
                ("ir_sun_shadow_sample", "sampleCascadeShadow"),
                ("ir_sun_face_query_layout", "sourceFaceRaySeparation"),
                ("ir_projected_face", "sourceFaceWithinProjectedBounds")):
            with self.subTest(function=function):
                production = (folder / f"{filename}.metal").read_text()
                self.assertEqual(extract_function(timed, function),
                                 extract_function(production, function))
        self.assertTrue(timed.endswith(PROBE.KERNEL))
        self.assertEqual(PROBE.CASES, (
            "envelope_miss", "footprint_miss", "behind", "first_blocker", "last_blocker"))

    def test_actual_layout_allocation_and_supported_integer_arithmetic(self):
        self.assertEqual(PROBE.buffer_bytes(PROBE.shader_source(False)), 30670864)
        self.assertEqual(PROBE.buffer_bytes(
            "constant uint kSourceFaceFirst = 19u / 4u;\n"
            "constant uint kSourceFaceBufferWords = (kSourceFaceFirst + 2u) * 3u;"), 72)

    def test_layout_parser_rejects_unsupported_expressions(self):
        for expression in ("1u << 4u", "8u - 1u", "-1", "1.5", "True", "abs(2)",
                           "[1][0]", "1 if True else 2", "2 ** 3"):
            with self.subTest(expression=expression), self.assertRaisesRegex(
                    ValueError, "unsupported layout constant"):
                PROBE.buffer_bytes(f"constant uint kSourceFaceBufferWords = {expression};")
        for source in ("", "constant uint kSourceFaceBufferWords = kSourceFaceMissing;"):
            with self.subTest(source=source), self.assertRaises(KeyError):
                PROBE.buffer_bytes(source)


class SunQueryProbeRoutingTest(unittest.TestCase):
    """Mocked launcher reports exercise Python routing, not native execution."""

    def test_gpu_validation_and_benchmark_modes_route_native_arguments(self):
        for hold, validate in (("gpu", True), ("benchmark", True), ("benchmark", False)):
            with self.subTest(hold=hold, validate=validate), tempfile.TemporaryDirectory() as temp:
                output = Path(temp).resolve() / "probe"
                argv = ["sun_query_probe.py", "--output", str(output),
                        "--queries", "256", "--rounds", "3"]
                if validate:
                    argv.append("--validate-only")
                samples = ([] if validate else [
                    {"count": count, "kind": kind, "gpu_ms": float(kind + 1)}
                    for count in (64, 256) for kind in range(5)])
                native_report = {"device": "mock; no native execution",
                                 "measurement": not validate, "samples": samples}

                def run(command, **kwargs):
                    if command[0] == "mock-clang++":
                        Path(command[-1]).write_bytes(b"mock binary; never executed")
                        return subprocess.CompletedProcess(command, 0)
                    self.assertEqual(command, [str(output / "native"),
                                               str(output / "timed.metal"),
                                               str(output / "diagnostic.metal"), "256", "3",
                                               "validate" if validate else "measure", "3.0"])
                    return subprocess.CompletedProcess(command, 0, json.dumps(native_report), "")

                console = io.StringIO()
                with (mock.patch.object(PROBE.sys, "argv", argv),
                      mock.patch.dict(PROBE.os.environ, {
                          "IR_RUN_LOCK_TOKEN": "mock-lock", "IR_ACQUIRE_HELD_VERB": hold},
                          clear=True),
                      mock.patch.object(PROBE.platform, "system", return_value="Darwin"),
                      mock.patch.object(PROBE.platform, "platform", return_value="mock-platform"),
                      mock.patch.object(PROBE.shutil, "which", return_value="mock-clang++"),
                      mock.patch.object(PROBE.subprocess, "run", side_effect=run) as launched,
                      mock.patch.object(PROBE.subprocess, "check_output", return_value="mock"),
                      redirect_stdout(console)):
                    PROBE.main()
                self.assertEqual(launched.call_count, 2)
                report = json.loads((output / "results.json").read_text())
                self.assertEqual(report["measurement"], not validate)
                self.assertEqual(report["samples"], samples)
                if validate:
                    self.assertEqual(report["summary"], [])
                    self.assertIn("no timings", console.getvalue())
                else:
                    self.assertEqual(len(report["summary"]), 10)
                    self.assertEqual(report["summary"][0]["median_gpu_ms"], 1.0)

    def test_gpu_hold_cannot_measure_and_missing_lock_cannot_validate(self):
        for hold, token, validate in (("gpu", "mock-lock", False), ("gpu", "", True),
                                      ("unrelated", "mock-lock", True)):
            with self.subTest(hold=hold, token=token, validate=validate):
                argv = ["sun_query_probe.py", "--output", "unused-output"]
                if validate:
                    argv.append("--validate-only")
                error = io.StringIO()
                with (mock.patch.object(PROBE.sys, "argv", argv),
                      mock.patch.dict(PROBE.os.environ, {
                          "IR_RUN_LOCK_TOKEN": token, "IR_ACQUIRE_HELD_VERB": hold}, clear=True),
                      mock.patch.object(PROBE.platform, "system", return_value="Darwin"),
                      mock.patch.object(PROBE.subprocess, "run") as launched,
                      redirect_stderr(error), self.assertRaises(SystemExit) as exited):
                    PROBE.main()
                self.assertEqual(exited.exception.code, 2)
                self.assertIn("ir-acquire benchmark", error.getvalue())
                launched.assert_not_called()


HOST_CASES = r"""
struct ProbeConfig {uint x,y;};
vec2 operator+(float a,vec2 b){return b+a;}
KERNELS
void require(bool value,const char* label){
    if(!value){std::cerr<<label<<"\n";std::exit(1);}
}
int main(){
    static_assert(kSourceFaceTileCapacity==64&&kSourceFaceTileQueryCapacity==256);
#if IR_SUN_QUERY_STATS
    static_assert(kSunQueryCounterCount==10);
#endif
    for(uint count:{64u,256u})for(uint kind=0;kind<5;++kind){
        sunDepthBuf.words.assign(kSourceFaceBufferWords,0xffffffffu);
        for(uint id=0;id<count;++id)initialize(sunDepthBuf.words.data(),{count,kind},id);
        require(sunDepthBuf.words[kSourceFaceHeaderOffset]==count,"fixture record count");
        require(sunDepthBuf.words[sourceFaceTileBase(0)]==count,"fixture tile count");
        EXPECTED
        for(uint id:{0u,255u,32768u,65280u,65535u}){
            std::vector<uint> output((id+1)*11,0xcdcdcdcdu);
            probe(sunDepthBuf,output.data(),id);
#if IR_SUN_QUERY_STATS
            require(output[id*11]==expectedShadow,"probe shadow mismatch");
            for(uint k=0;k<10;++k)
                require(output[id*11+1+k]==expected[k],"probe counter mismatch");
#else
            require(output[id]==expectedShadow,"probe shadow mismatch");
            require(output[id+1]==0xcdcdcdcdu,"timed probe writes only one output word");
#endif
        }
    }
}
"""


def fixture_source(diagnostic, kernel=None, expected=None):
    kernel = PROBE.KERNEL if kernel is None else kernel
    kernel = re.sub(r"\[\[[^]]+\]\]", "", kernel)
    kernel = (kernel.replace("kernel void", "void")
              .replace("device const uint* buffer", "BoundBuffer& buffer")
              .replace("device uint*", "uint*")
              .replace("constant uint2&", "const ProbeConfig&")
              .replace("as_type<uint>", "floatBitsToUint")
              .replace("float2(0)", "float2(0.f)").replace("float2(1)", "float2(1.f)")
              .replace("float4(0,0,0,1)", "vec4{0,0,0,1}"))
    if expected is None:
        expected = re.search(r"const uint32_t expectedShadow=.*?const uint32_t expected\[\]="
                             r".*?;", PROBE.LAUNCHER, re.DOTALL)[0]
    cases = HOST_CASES.replace("KERNELS", kernel).replace("EXPECTED", expected)
    source = receiver.host_source("metal", "metal/", False, cases=cases, query_stats=diagnostic)
    return source.replace(
        "struct vec3 {float x,y,z;", "struct vec3 {float x,y,z;"
        "explicit vec3(float a):x(a),y(a),z(a){}"
        "float operator[](uint i)const{return i==0?x:(i==1?y:z);}")


@unittest.skipUnless(receiver.COMPILER, "probe fixture controls require a C++ compiler")
class SunQueryProbeFixtureTest(unittest.TestCase):
    compile_and_run = receiver.SourceFaceOverflowTest.compile_and_run

    def test_actual_initializer_and_probe_match_all_launcher_expectations(self):
        for diagnostic in (False, True):
            with self.subTest(diagnostic=diagnostic):
                run = self.compile_and_run(fixture_source(diagnostic))
                self.assertEqual(run.returncode, 0, run.stderr)

    def test_wrong_spill_record_and_counter_expectations_fail(self):
        mutations = (
            ("spill_record", PROBE.KERNEL.replace("buffer[node]=id;", "buffer[node]=0u;"),
             None, "shadow mismatch"),
            ("counter_slot", PROBE.KERNEL.replace("1u+counter", "counter"),
             None, "counter mismatch"),
            ("expectation", PROBE.KERNEL,
             "const uint expectedShadow=kind>=3?0x3f800000u:0u;"
             "const uint expected[]={0,0,0,0,0,0,0,0,0,0};", "counter mismatch"),
        )
        for name, kernel, expected, failure in mutations:
            with self.subTest(mutation=name):
                if expected is None:
                    self.assertNotEqual(kernel, PROBE.KERNEL)
                run = self.compile_and_run(fixture_source(True, kernel, expected))
                self.assertNotEqual(run.returncode, 0, "invalid fixture escaped controls")
                self.assertIn(failure, run.stderr)


if __name__ == "__main__":
    unittest.main()
