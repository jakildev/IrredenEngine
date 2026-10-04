"""Execute Metal pipeline creation and registered workgroup limits with stub devices."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_function

ROOT = Path(__file__).resolve().parents[2]
PIPELINE = ROOT / "engine/render/src/metal/metal_pipeline.cpp"
COMPILER = shutil.which("c++")


def harness():
    source = PIPELINE.read_text()
    registry = extract_function(source.replace(
        "MTL::Size threadgroupSizeForFunctionName(",
        "uint threadgroupSizeForFunctionName("), "threadgroupSizeForFunctionName")
    registry = registry.replace("uint threadgroupSizeForFunctionName(",
                                "MTL::Size threadgroupSizeForFunctionName(")
    method = extract_function(source.replace(
        "MTL::ComputePipelineState *getComputePipelineState() override",
        "uint getComputePipelineState()"), "getComputePipelineState")
    method = method.replace("uint getComputePipelineState()",
                            "MTL::ComputePipelineState *getComputePipelineState()")
    return PREAMBLE + registry + "\nstruct Pipeline {\n" + MEMBERS + method + "\n};\n" + CASES


PREAMBLE = r"""
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
// The runtime limit must hold when release builds strip diagnostic assertions.
#define IR_ASSERT(...) ((void)0)
#define IRE_LOG_FATAL(...) ((void)0)
namespace NS {
struct String {
    std::string value;
    const char* utf8String() const { return value.c_str(); }
};
struct Error {
    String description{"stub creation error"};
    String* localizedDescription() { return &description; }
};
}
namespace MTL {
struct Size {
    std::size_t width, height, depth;
    Size(std::size_t x, std::size_t y, std::size_t z): width(x), height(y), depth(z) {}
};
struct Function {
    NS::String label;
    NS::String* name() { return &label; }
};
struct ComputePipelineState {
    std::size_t limit;
    int releases = 0;
    int limitQueries = 0;
    std::size_t maxTotalThreadsPerThreadgroup() { ++limitQueries; return limit; }
    void release() { ++releases; }
};
struct Device {
    std::size_t limit = 192;
    bool failCreation = false;
    bool reportError = false;
    int attempts = 0;
    NS::Error creationError;
    std::vector<std::unique_ptr<ComputePipelineState>> created;
    ComputePipelineState* newComputePipelineState(Function*, NS::Error** error) {
        ++attempts;
        if (failCreation) {
            *error = reportError ? &creationError : nullptr;
            return nullptr;
        }
        created.push_back(std::make_unique<ComputePipelineState>());
        created.back()->limit = limit;
        return created.back().get();
    }
};
}
MTL::Device g_device;
MTL::Device* metalDevice() { return &g_device; }
"""

MEMBERS = r"""
    MTL::Function* m_computeFunction = nullptr;
    MTL::ComputePipelineState* m_computeState = nullptr;
    MTL::Size m_computeThreadsPerThreadgroup{1, 1, 1};
"""

CASES = r"""
int main() {
    Pipeline graphics;
    if (graphics.getComputePipelineState() != nullptr || g_device.attempts != 0) return 1;
    for (const char* name : {
            "c_voxel_to_trixel_stage_1", "c_voxel_to_trixel_stage_1_feeder",
            "c_voxel_to_trixel_stage_1_winner_resolve", "c_voxel_to_trixel_stage_2",
            "c_voxel_to_trixel_stage_2_winner"}) {
        g_device = MTL::Device{};
        MTL::Function function{{name}};
        const auto size = threadgroupSizeForFunctionName(name);
        if (size.width != 2 || size.height != 3 || size.depth != 32) return 2;
        Pipeline exact{&function, nullptr, size};
        MTL::ComputePipelineState* accepted = nullptr;
        try { accepted = exact.getComputePipelineState(); }
        catch (const std::runtime_error&) { return 3; }
        if (!accepted || accepted != exact.m_computeState || accepted->releases != 0 ||
            accepted->limitQueries != 1 || g_device.created.size() != 1) return 4;
        if (exact.getComputePipelineState() != accepted || g_device.created.size() != 1 ||
            accepted->limitQueries != 1) return 5;

        g_device.limit = 191;
        Pipeline rejected{&function, nullptr, size};
        bool threw = false;
        try { rejected.getComputePipelineState(); }
        catch (const std::runtime_error& error) {
            threw = true;
            const std::string message = error.what();
            if (message.find(name) == std::string::npos ||
                message.find("192") == std::string::npos ||
                message.find("191") == std::string::npos) return 6;
        }
        if (!threw || rejected.m_computeState != nullptr || g_device.created.size() != 2 ||
            g_device.created.back()->releases != 1 ||
            g_device.created.back()->limitQueries != 1) return 7;

        g_device.limit = 192;
        auto* retried = rejected.getComputePipelineState();
        if (!retried || retried != rejected.m_computeState ||
            retried == g_device.created[1].get() || retried->releases != 0 ||
            retried->limitQueries != 1 ||
            g_device.created.size() != 3) return 8;
        if (rejected.getComputePipelineState() != retried || g_device.created.size() != 3 ||
            retried->limitQueries != 1) return 9;

        for (bool reportError : {false, true}) {
            g_device = MTL::Device{};
            g_device.failCreation = true;
            g_device.reportError = reportError;
            Pipeline failedCreation{&function, nullptr, size};
            bool creationThrew = false;
            try { failedCreation.getComputePipelineState(); }
            catch (const std::runtime_error& error) {
                creationThrew = true;
                const std::string message = error.what();
                const char* description = reportError ? "stub creation error" : "<unknown>";
                if (message.find(name) == std::string::npos ||
                    message.find(description) == std::string::npos) return 10;
            }
            if (!creationThrew || failedCreation.m_computeState != nullptr ||
                !g_device.created.empty() || g_device.attempts != 1) return 11;
            g_device.failCreation = false;
            auto* recovered = failedCreation.getComputePipelineState();
            if (!recovered || recovered != failedCreation.m_computeState ||
                recovered->releases != 0 || recovered->limitQueries != 1 ||
                g_device.created.size() != 1 || g_device.attempts != 2) return 12;
            if (failedCreation.getComputePipelineState() != recovered ||
                g_device.attempts != 2 || recovered->limitQueries != 1) return 13;
        }
    }
}
"""


@unittest.skipUnless(COMPILER, "Metal pipeline controls require a C++ compiler")
class MetalPipelineLimitsTest(unittest.TestCase):
    def test_registered_workgroups_and_pipeline_lifetime(self):
        source = harness()
        for name, candidate, expected in (
            ("production", source, 0),
            ("missing_limit_guard", source.replace("threadCount > threadLimit", "false"), 7),
            ("rejects_exact_limit", source.replace("threadCount > threadLimit",
                                                 "threadCount >= threadLimit"), 3),
            ("missing_creation_guard", source.replace("if (pipelineState == nullptr)",
                                                     "if (false)"), None),
        ):
            with self.subTest(variant=name), tempfile.TemporaryDirectory() as tmp:
                if name != "production":
                    self.assertNotEqual(candidate, source)
                cpp, binary = Path(tmp) / "control.cpp", Path(tmp) / "control"
                cpp.write_text(candidate)
                build = subprocess.run([COMPILER, "-std=c++17", str(cpp), "-o", str(binary)],
                                       capture_output=True, text=True)
                self.assertEqual(build.returncode, 0, build.stderr)
                result = subprocess.run([str(binary)], cwd=tmp, capture_output=True, text=True)
                if expected is None:
                    self.assertNotEqual(result.returncode, 0, result.stderr)
                else:
                    self.assertEqual(result.returncode, expected, result.stderr)


if __name__ == "__main__":
    unittest.main()
