#!/usr/bin/env python3
"""Compare actual production Metal hierarchy builds with an independent oracle.

Run under ``ir-acquire gpu --`` for correctness, or ``ir-acquire benchmark --``
with --measure for isolated build timings. The base and working-tree shaders are
expanded without replacing their kernels; timing is not an engine frame metric.
"""

import argparse
import ast
import hashlib
import json
import math
import os
import platform
import re
import shutil
import statistics
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SHADERS = "engine/render/src/shaders/metal"
KERNEL = "c_build_sun_hierarchy.metal"
LAYOUT = "ir_sun_face_query_layout.metal"
RECORD = struct.Struct("<9fI")
NODE = struct.Struct("<8f")

NATIVE = r"""#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <vector>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static NSUInteger number(NSDictionary* object, NSString* key) {
    return [object[key] unsignedLongLongValue];
}
static double execute(id<MTLCommandQueue> queue, id<MTLComputePipelineState> pipeline,
                      id<MTLBuffer> buffer, NSArray* plan, NSUInteger repeats) {
    @autoreleasepool {
    id<MTLCommandBuffer> command = [queue commandBuffer];
    for (NSUInteger repeat = 0; repeat < repeats; ++repeat) {
        for (NSDictionary* pass in plan) {
            id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
            [encoder setComputePipelineState:pipeline];
            [encoder setBuffer:buffer offset:256 atIndex:28];
            // setBytes snapshots each pass; a shared mutable parameter buffer does not.
            const int frame[4] = {[pass[@"argument"] intValue], 0, 0, 0};
            [encoder setBytes:frame length:sizeof(frame) atIndex:16];
            [encoder dispatchThreadgroups:MTLSizeMake(number(pass, @"groups"), 1, 1)
                    threadsPerThreadgroup:MTLSizeMake(number(pass, @"threads"), 1, 1)];
            // Separate encoders preserve tracked-buffer producer/consumer ordering.
            [encoder endEncoding];
        }
    }
    [command commit];
    [command waitUntilCompleted];
    if (command.status != MTLCommandBufferStatusCompleted)
        throw std::runtime_error(command.error.localizedDescription.UTF8String);
    return (command.GPUEndTime - command.GPUStartTime) * 1000.0 / repeats;
    }
}
static void unchanged(const std::vector<unsigned char>& before, id<MTLBuffer> buffer,
                      NSUInteger begin, NSUInteger end) {
    require(std::memcmp(before.data() + begin,
            static_cast<unsigned char*>(buffer.contents) + begin, end - begin) == 0,
            "build modified bytes outside readiness and the used hierarchy nodes");
}
static NSData* readback(id<MTLBuffer> buffer, NSUInteger ready, NSUInteger tree,
                       NSUInteger treeBytes) {
    NSMutableData* data = [NSMutableData dataWithBytes:
        static_cast<unsigned char*>(buffer.contents) + ready length:4];
    [data appendBytes:static_cast<unsigned char*>(buffer.contents) + tree length:treeBytes];
    return data;
}
int main(int argc, const char** argv) {
    @autoreleasepool {
        try {
            require(argc == 2, "expected job.json");
            NSError* error = nil;
            NSData* jobData = [NSData dataWithContentsOfFile:@(argv[1])];
            NSDictionary* job = [NSJSONSerialization JSONObjectWithData:jobData
                                                              options:0 error:&error];
            require(job != nil, "invalid job JSON");
            id<MTLDevice> device = MTLCreateSystemDefaultDevice();
            require(device != nil, "Metal device unavailable");
            id<MTLCommandQueue> queue = [device newCommandQueue];
            NSDictionary* layout = job[@"layout"];
            const NSUInteger size = number(layout, @"kSourceFaceBufferWords") * 4 + 512;
            const NSUInteger header = 256 + number(layout, @"kSourceFaceHeaderOffset") * 4;
            const NSUInteger records = 256 + number(layout, @"kSourceFaceRecordOffset") * 4;
            const NSUInteger needed = 256 + number(layout, @"kSourceFaceHierarchyNeededOffset") * 4;
            const NSUInteger ready = 256 + number(layout, @"kSourceFaceHierarchyReadyOffset") * 4;
            const NSUInteger tree = 256 + number(layout, @"kSourceFaceHierarchyOffset") * 4;
            const NSUInteger treeBytes = number(layout, @"kSourceFaceHierarchyMaxLeaves") * 2 * 32;
            const NSUInteger capacity = number(layout, @"kSourceFaceCapacity");
            const NSUInteger leafSize = number(layout, @"kSourceFaceHierarchyLeafSize");
            NSMutableArray* results = [NSMutableArray array];
            NSMutableArray* pipelines = [NSMutableArray array];
            NSArray* arms = @[@"base", @"head"];
            for (NSString* mode in job[@"math_modes"]) {
                id<MTLComputePipelineState> pipeline[2];
                id<MTLBuffer> buffer[2];
                for (NSUInteger arm = 0; arm < 2; ++arm) {
                    NSString* source = [NSString stringWithContentsOfFile:job[@"sources"][arms[arm]]
                                               encoding:NSUTF8StringEncoding error:&error];
                    require(source != nil, "cannot read expanded production source");
                    MTLCompileOptions* options = [MTLCompileOptions new];
                    options.mathMode = [mode isEqualToString:@"fast"]
                        ? MTLMathModeFast : MTLMathModeSafe;
                    id<MTLLibrary> library = [device newLibraryWithSource:source
                                                               options:options error:&error];
                    if (!library) throw std::runtime_error(error.localizedDescription.UTF8String);
                    id<MTLFunction> function =
                        [library newFunctionWithName:@"c_build_sun_hierarchy"];
                    pipeline[arm] = [device newComputePipelineStateWithFunction:function
                                                                        error:&error];
                    if (!pipeline[arm])
                        throw std::runtime_error(error.localizedDescription.UTF8String);
                    for (NSDictionary* pass in job[@"plans"][arms[arm]])
                        require(number(pass, @"threads") <=
                                    pipeline[arm].maxTotalThreadsPerThreadgroup,
                                "dispatch exceeds pipeline threadgroup limit");
                    [pipelines addObject:@{@"arm":arms[arm], @"math":mode,
                        @"max_threads":@(pipeline[arm].maxTotalThreadsPerThreadgroup),
                        @"thread_execution_width":@(pipeline[arm].threadExecutionWidth),
                        @"threadgroup_bytes":@(pipeline[arm].staticThreadgroupMemoryLength)}];
                    buffer[arm] = [device newBufferWithLength:size
                                                     options:MTLResourceStorageModeShared];
                    require(buffer[arm] != nil, "cannot allocate production sun buffer");
                }
                for (NSDictionary* frame in job[@"frames"]) {
                    const NSUInteger count = number(frame, @"records");
                    const bool active = count > 0 && count <= capacity &&
                                        number(frame, @"needed") != 0;
                    NSUInteger leaves = 1;
                    while (leaves * leafSize < count && active) leaves *= 2;
                    NSData* input = [NSData dataWithContentsOfFile:frame[@"input"]];
                    require(input != nil && input.length <= capacity * 40, "invalid record input");
                    NSData* checked[2];
                    for (NSUInteger arm = 0; arm < 2; ++arm) {
                        auto* bytes = static_cast<unsigned char*>(buffer[arm].contents);
                        if ([frame[@"reset"] boolValue]) std::memset(bytes, 0xcd, size);
                        if (input.length) std::memcpy(bytes + records, input.bytes, input.length);
                        *reinterpret_cast<uint32_t*>(bytes + header) = uint32_t(count);
                        *reinterpret_cast<uint32_t*>(bytes + needed) =
                            uint32_t(number(frame, @"needed"));
                        *reinterpret_cast<uint32_t*>(bytes + ready) = 0;
                        const std::vector<unsigned char> before(bytes, bytes + size);
                        execute(queue, pipeline[arm], buffer[arm], job[@"plans"][arms[arm]], 1);
                        unchanged(before, buffer[arm], 0, ready);
                        unchanged(before, buffer[arm], ready + 4, tree + (active ? 32 : 0));
                        unchanged(before, buffer[arm], tree + (active ? 2 * leaves * 32 : 0), size);
                        require(*reinterpret_cast<uint32_t*>(bytes + ready) ==
                                    (active ? leaves : 0),
                                "incorrect hierarchy readiness");
                        checked[arm] = readback(buffer[arm], ready, tree, treeBytes);
                        NSString* name = [NSString stringWithFormat:@"%@-%@-%@.bin",
                                          mode, frame[@"name"], arms[arm]];
                        NSString* path = [job[@"output"] stringByAppendingPathComponent:name];
                        require([checked[arm] writeToFile:path atomically:YES],
                                "cannot save readback");
                    }
                    // Whole-tree equality includes stale nodes retained across shrinking frames.
                    require([checked[0] isEqualToData:checked[1]], "base/head node bytes differ");
                    NSMutableArray* samples = [NSMutableArray array];
                    double burnSeconds = 0;
                    if ([job[@"measure"] boolValue] && [frame[@"measure"] boolValue]) {
                        std::vector<unsigned char> timingBefore[2];
                        for (NSUInteger arm = 0; arm < 2; ++arm) {
                            auto* bytes = static_cast<unsigned char*>(buffer[arm].contents);
                            timingBefore[arm].assign(bytes, bytes + size);
                        }
                        const NSUInteger repeats = number(job, @"builds_per_sample");
                        const auto start = std::chrono::steady_clock::now();
                        NSUInteger warmRounds = 0;
                        do {
                            for (NSUInteger i = 0; i < 2; ++i) {
                                const NSUInteger arm = (warmRounds + i) % 2;
                                execute(queue, pipeline[arm], buffer[arm],
                                        job[@"plans"][arms[arm]], repeats);
                            }
                            ++warmRounds;
                            burnSeconds = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - start).count();
                        } while (burnSeconds < 3.0 || warmRounds < 2);
                        for (NSUInteger round = 0; round < number(job, @"rounds"); ++round) {
                            for (NSUInteger i = 0; i < 2; ++i) {
                                const NSUInteger arm = (round + i) % 2;
                                const auto begin = std::chrono::steady_clock::now();
                                const double gpu = execute(queue, pipeline[arm], buffer[arm],
                                    job[@"plans"][arms[arm]], repeats);
                                const double wall = std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - begin).count() / repeats;
                                require(gpu > 0, "GPU timestamps unavailable");
                                [samples addObject:@{@"round":@(round + 1), @"position":@(i + 1),
                                    @"arm":arms[arm], @"gpu_ms_per_build":@(gpu),
                                    @"wall_ms_per_build":@(wall)}];
                            }
                        }
                        // Readbacks and comparisons are outside every timed command.
                        for (NSUInteger arm = 0; arm < 2; ++arm)
                            unchanged(timingBefore[arm], buffer[arm], 0, size);
                    }
                    [results addObject:@{@"case":frame[@"name"], @"math":mode,
                        @"records":@(count), @"leaves":@(active ? leaves : 0),
                        @"burn_in_seconds":@(burnSeconds), @"samples":samples}];
                    fprintf(stderr, "validated %s %s\n", mode.UTF8String,
                            [frame[@"name"] UTF8String]);
                }
            }
            NSDictionary* report = @{@"device":device.name, @"registry_id":@(device.registryID),
                                     @"pipelines":pipelines, @"results":results};
            NSData* reportData = [NSJSONSerialization dataWithJSONObject:report
                                    options:NSJSONWritingPrettyPrinted error:&error];
            NSString* reportPath = [job[@"output"] stringByAppendingPathComponent:@"native.json"];
            require([reportData writeToFile:reportPath atomically:YES],
                    "cannot save native report");
            return 0;
        } catch (const std::exception& error) {
            fprintf(stderr, "%s\n", error.what());
            return 1;
        }
    }
}
"""


def digest(data):
    return hashlib.sha256(data).hexdigest()


def capture(command):
    return subprocess.check_output(command, cwd=ROOT, text=True).strip()


def expand_shader(read, name=KERNEL, dependencies=None, stack=()):
    if dependencies is None:
        dependencies = {}
    if name in stack:
        raise ValueError(f"cyclic shader include: {name}")
    source = read(name)
    dependencies[name] = digest(source.encode())
    return re.sub(
        r'^\s*#include "([\w.]+)"\s*$',
        lambda match: expand_shader(read, match[1], dependencies, (*stack, name)),
        source,
        flags=re.MULTILINE,
    )


def parse_layout(source):
    constants = {}

    def evaluate(node):
        if isinstance(node, ast.Constant) and type(node.value) is int:
            return node.value
        if isinstance(node, ast.Name):
            return constants[node.id]
        if isinstance(node, ast.BinOp):
            left, right = evaluate(node.left), evaluate(node.right)
            if isinstance(node.op, ast.Add):
                return left + right
            if isinstance(node.op, ast.Sub):
                return left - right
            if isinstance(node.op, ast.Mult):
                return left * right
            if isinstance(node.op, ast.Div) and right and left % right == 0:
                return left // right
        raise ValueError(f"unsupported layout expression: {ast.dump(node)}")

    for name, expression in re.findall(r"constant uint (\w+)\s*=\s*([^;]+);", source):
        expression = re.sub(r"\b(0x[0-9A-Fa-f]+|\d+)u\b", r"\1", expression)
        constants[name] = evaluate(ast.parse(expression, mode="eval").body)
    return constants


def dispatch_plans(base, head):
    if "kSourceFaceHierarchyBuildPasses" in base:
        raise ValueError("base must use the fourteen per-level dispatches, not cooperative passes")
    for name, value in base.items():
        if head.get(name) != value:
            raise ValueError(f"storage layout changed: {name}")
    if base["kSourceFaceRecordWords"] != 10 or base["kSourceFaceHierarchyNodeWords"] != 8:
        raise ValueError("probe requires production 40-byte records and 32-byte nodes")
    leaves = base["kSourceFaceHierarchyMaxLeaves"]
    width = head["kSourceFaceHierarchyWorkgroupSize"]
    if leaves != 8192 or width != 128 or head["kSourceFaceHierarchyBuildPasses"] != 2:
        raise ValueError("probe requires the 14-pass base and two-pass 128-thread candidate")
    return {
        "base": [
            {
                "argument": 0 if level == 0 else leaves >> level,
                "groups": max(1, ((leaves >> level) + 63) // 64),
                "threads": 64,
            }
            for level in range(leaves.bit_length())
        ],
        "head": [
            {"argument": 0, "groups": leaves // width, "threads": width},
            {"argument": 1, "groups": 1, "threads": width},
        ],
    }


def make_records(count, mixed=False, reverse=False):
    result = []
    for index in range(count):
        sign = -1 if index % 2 else 1
        corner = [
            (index % 257 - 128) * 0.25,
            (index // 257 % 257 - 128) * 0.5,
            (index % 113 - 56) * 0.125,
        ]
        edge_u = [sign * (1 + index % 7 * 0.25), sign * 0.25, 0.125]
        edge_v = [-0.5, 0.75 + index % 5 * 0.25, -0.25]
        if index % 3 == 0:
            corner = [c + u for c, u in zip(corner, edge_u)]
            edge_u = [-u for u in edge_u]
        kind = (1 if index % 17 == 0 else 2 if index % 29 == 0 else 0) if mixed else 0
        result.append(RECORD.pack(*corner, *edge_u, *edge_v, kind))
    return b"".join(reversed(result) if reverse else result)


def make_frames(output):
    frames = []

    def add(name, count, *, needed=1, mixed=False, reverse=False, reset=True, measure=False):
        data = make_records(count if count <= 65536 else 0, mixed, reverse)
        path = output / f"{name}-records.bin"
        path.write_bytes(data)
        frames.append(
            {
                "name": name,
                "records": count,
                "needed": needed,
                "reset": reset,
                "measure": measure,
                "input": str(path),
                "input_sha256": digest(data),
            }
        )

    add("empty", 0, measure=True)
    add("not-needed", 1025, needed=0, measure=True)
    add("pool-overflow", 65537)
    for count in (1, 8, 9, 1023, 1024, 1025, 65536):
        add(f"finite-{count}", count, measure=True)
    add("mixed-1025", 1025, mixed=True, measure=True)
    add("mixed-reversed-1025", 1025, mixed=True, reverse=True)
    add("shrink-65536", 65536, mixed=True)
    for count in (1025, 9, 1, 0):
        add(f"shrink-{count}", count, reset=False)
    add("shrink-not-needed", 1025, needed=0, reset=False)
    add("shrink-overflow", 65537, reset=False)
    return frames


def leaf_count(records, leaf_size=8):
    return 1 << (max(1, (records + leaf_size - 1) // leaf_size) - 1).bit_length()


def validate_tree(data, records, count, needed, layout):
    """Use exact dyadic fixture corners, independently of shader padding math."""
    expected_size = 4 + 2 * layout["kSourceFaceHierarchyMaxLeaves"] * NODE.size
    if len(data) != expected_size:
        raise ValueError("readback does not contain the full production hierarchy")
    leaves = leaf_count(count, layout["kSourceFaceHierarchyLeafSize"])
    active = needed and 0 < count <= layout["kSourceFaceCapacity"]
    ready = struct.unpack_from("<I", data)[0]
    if ready != (leaves if active else 0):
        raise ValueError(f"readiness {ready} differs from expected {leaves if active else 0}")
    if not active:
        return 0
    if len(records) != count * RECORD.size:
        raise ValueError("record count differs from the packed input")
    nodes = list(NODE.iter_unpack(data[4 : 4 + 2 * leaves * NODE.size]))
    expected = [None] * (2 * leaves)
    for index, record in enumerate(RECORD.iter_unpack(records)):
        node = leaves + index // layout["kSourceFaceHierarchyLeafSize"]
        state = 1 if record[9] else 0
        # Fixture inputs are small multiples of 1/8; these sums are exact in double.
        low = [record[a] + min(0, record[3 + a]) + min(0, record[6 + a]) for a in range(3)]
        high = [record[a] + max(0, record[3 + a]) + max(0, record[6 + a]) for a in range(3)]
        if expected[node] is None:
            expected[node] = (state, low, high)
        else:
            previous = expected[node]
            expected[node] = (
                max(state, previous[0]),
                [min(a, b) for a, b in zip(low, previous[1])],
                [max(a, b) for a, b in zip(high, previous[2])],
            )
    for node in range(leaves - 1, 0, -1):
        left, right = expected[2 * node : 2 * node + 2]
        if left is None or right is None:
            expected[node] = right if left is None else left
        else:
            expected[node] = (
                max(left[0], right[0]),
                [min(a, b) for a, b in zip(left[1], right[1])],
                [max(a, b) for a, b in zip(left[2], right[2])],
            )
    for node in range(1, 2 * leaves):
        actual, want = nodes[node], expected[node]
        if not all(math.isfinite(value) for value in actual) or actual[7] != 0:
            raise ValueError(f"node {node}: nonfinite bound or high.w")
        if actual[3] != (-1 if want is None else want[0]):
            raise ValueError(f"node {node}: wrong empty/bounded/unbounded state")
        if want is None:
            if actual != (0, 0, 0, -1, 0, 0, 0, 0):
                raise ValueError(f"node {node}: empty bounds not canonical")
            continue
        if want[0] == 0:
            for axis in range(3):
                if actual[axis] > want[1][axis] or actual[4 + axis] < want[2][axis]:
                    raise ValueError(f"node {node}: bounds clip an independent descendant corner")
        if node < leaves:
            children = [child for child in nodes[2 * node : 2 * node + 2] if child[3] >= 0]
            for axis in range(3):
                if actual[axis] != min(child[axis] for child in children) or actual[
                    axis + 4
                ] != max(child[axis + 4] for child in children):
                    raise ValueError(f"node {node}: does not merge its actual children")
    return 2 * leaves - 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-ref", default="e766f2bb8")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--measure", action="store_true")
    parser.add_argument("--rounds", type=int, default=8)
    parser.add_argument("--builds-per-sample", type=int, default=32)
    args = parser.parse_args(argv)
    verbs = ("benchmark",) if args.measure else ("gpu", "benchmark")
    if (
        not os.environ.get("IR_RUN_LOCK_TOKEN")
        or os.environ.get("IR_ACQUIRE_HELD_VERB") not in verbs
    ):
        parser.error("run under ir-acquire benchmark -- (gpu allowed for validation)")
    if platform.system() != "Darwin":
        parser.error("native Metal validation requires macOS")
    if args.rounds < 2 or args.rounds % 2 or args.builds_per_sample < 1:
        parser.error("use positive builds-per-sample and an even rounds count >= 2")
    compiler = shutil.which("clang++")
    if compiler is None:
        parser.error("clang++ is required")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    base_ref = capture(["git", "rev-parse", "--verify", f"{args.base_ref}^{{commit}}"])
    sources, layouts, hashes = {}, {}, {}
    for arm in ("base", "head"):

        def read(name, arm=arm):
            path = f"{SHADERS}/{name}"
            if arm == "base":
                return subprocess.check_output(
                    ["git", "show", f"{base_ref}:{path}"], cwd=ROOT
                ).decode()
            return (ROOT / path).read_text()

        dependencies = {}
        source = "#define IR_SUN_QUERY_STATS 0\n" + expand_shader(read, dependencies=dependencies)
        path = output / f"{arm}.metal"
        path.write_text(source)
        sources[arm] = str(path)
        layouts[arm] = parse_layout(read(LAYOUT))
        hashes[arm] = {"expanded_sha256": digest(source.encode()), "dependencies": dependencies}
    plans = dispatch_plans(layouts["base"], layouts["head"])
    frames = make_frames(output)
    job = {
        "output": str(output),
        "sources": sources,
        "plans": plans,
        "layout": layouts["head"],
        "frames": frames,
        "math_modes": ["fast", "safe"],
        "measure": args.measure,
        "rounds": args.rounds,
        "builds_per_sample": args.builds_per_sample,
    }
    job_path = output / "job.json"
    job_path.write_text(json.dumps(job, indent=2) + "\n")
    launcher, binary = output / "probe.mm", output / "probe"
    launcher.write_text(NATIVE)
    compile_command = [
        compiler,
        "-std=c++17",
        "-O2",
        "-fobjc-arc",
        str(launcher),
        "-framework",
        "Metal",
        "-framework",
        "Foundation",
        "-o",
        str(binary),
    ]
    metadata = {
        "base_ref": base_ref,
        "head": capture(["git", "rev-parse", "HEAD"]),
        "status": capture(["git", "status", "--short"]),
        "argv": sys.argv,
        "platform": platform.platform(),
        "sources": hashes,
        "script_sha256": digest(Path(__file__).read_bytes()),
        "launcher_sha256": digest(NATIVE.encode()),
        "job_sha256": digest(job_path.read_bytes()),
        "compile_command": compile_command,
        "compiler": capture([compiler, "--version"]),
        "power_before": capture(["pmset", "-g", "batt"]),
    }
    metadata_path = output / "metadata.json"
    metadata_path.write_text(json.dumps(metadata, indent=2) + "\n")
    subprocess.run(compile_command, cwd=ROOT, check=True)
    metadata["binary_sha256"] = digest(binary.read_bytes())
    metadata_path.write_text(json.dumps(metadata, indent=2) + "\n")
    try:
        with (output / "native.log").open("w") as log:
            subprocess.run(
                [str(binary), str(job_path)], cwd=ROOT, check=True, stdout=log, stderr=log
            )
    finally:
        metadata["power_after"] = capture(["pmset", "-g", "batt"])
        metadata_path.write_text(json.dumps(metadata, indent=2) + "\n")
    checked = 0
    for mode in job["math_modes"]:
        for frame in frames:
            records = Path(frame["input"]).read_bytes()
            base = (output / f"{mode}-{frame['name']}-base.bin").read_bytes()
            head = (output / f"{mode}-{frame['name']}-head.bin").read_bytes()
            if base != head:
                raise ValueError(f"{mode}/{frame['name']}: base/head readiness or tree differ")
            checked += validate_tree(
                head, records, frame["records"], frame["needed"], layouts["head"]
            )
    report = json.loads((output / "native.json").read_text())
    lines = [
        "# Production Metal hierarchy build comparison",
        "",
        f"Device: {report['device']}. Base: `{base_ref}`.",
        "",
        f"PASS: {len(frames) * 2} paired builds; {checked} used nodes checked independently.",
        "All readiness/tree bytes match; guards, non-hierarchy storage and unused nodes "
        "are unchanged.",
        "Fast and safe math compile the expanded production shaders independently.",
        "",
    ]
    if args.measure:
        lines.extend(
            [
                "GPU command envelope per build (not engine frame timing).",
                "At least 3 seconds burn-in per case/mode; alternating forward/reverse arm order.",
                "",
                "| Case | Math | Base median ms | Head median ms |",
                "|---|---|---:|---:|",
            ]
        )
        for result in report["results"]:
            if result["samples"]:
                medians = [
                    statistics.median(
                        sample["gpu_ms_per_build"]
                        for sample in result["samples"]
                        if sample["arm"] == arm
                    )
                    for arm in ("base", "head")
                ]
                lines.append(
                    f"| {result['case']} | {result['math']} | {medians[0]:.6f} | {medians[1]:.6f} |"
                )
    summary = "\n".join(lines) + "\n"
    (output / "summary.md").write_text(summary)
    print(summary)


if __name__ == "__main__":
    main()
