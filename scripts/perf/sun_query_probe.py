"""Measure native Metal complete-tile queries with production sampling code.

Run under ir-acquire benchmark. Synthetic coherent receivers share one tile;
this is a query microbenchmark, not raster, index-build or engine throughput.
Diagnostic counters run separately from timed, counters-disabled dispatches.
"""

import argparse
import hashlib
import json
import os
import platform
import shutil
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CASES = ("envelope_miss", "footprint_miss", "behind", "first_blocker", "last_blocker")

KERNEL = r"""
kernel void initialize(device uint* buffer [[buffer(0)]],
                       constant uint2& config [[buffer(1)]],
                       uint id [[thread_position_in_grid]]) {
    const uint count=config.x, kind=config.y;
    if(id==0){
        buffer[kSourceFaceHeaderOffset]=count;
        buffer[kSourceFaceCapacityOffset]=kSourceFaceCapacity;
        buffer[sourceFaceTileBase(0)]=count;
        buffer[kSourceFaceSpillCounterOffset]=count>kSourceFaceTileCapacity
            ? count-kSourceFaceTileCapacity : 0u;
        buffer[kSourceFaceSpillHeadOffset]=0u;
    }
    if(id<kSourceFaceTileCapacity)buffer[sourceFaceTileBase(0)+1u+id]=id;
    else {
        const uint node=kSourceFaceSpillNodeOffset+(id-kSourceFaceTileCapacity)*2u;
        buffer[node]=id;
        buffer[node+1u]=id+1u<count?id+1u-kSourceFaceTileCapacity:0xFFFFFFFFu;
    }
    float3 corner=float3(2,2,0), u=float3(1,0,0), v=float3(0,1,0);
    if(kind==1u){corner=float3(0,2,0);u=float3(2,2,0);v=float3(2,-2,0);}
    if(kind==2u || (kind==3u && id==0u) || (kind==4u && id+1u==count)){
        corner=float3(0,0,kind==2u?3.0:0.0);u=float3(4,0,0);v=float3(0,4,0);
    }
    const uint record=kSourceFaceRecordOffset+id*kSourceFaceRecordWords;
    for(uint axis=0;axis<3;++axis){
        buffer[record+axis]=as_type<uint>(corner[axis]);
        buffer[record+3u+axis]=as_type<uint>(u[axis]);
        buffer[record+6u+axis]=as_type<uint>(v[axis]);
    }
    buffer[record+9u]=0u;
}
kernel void probe(device const uint* buffer [[buffer(0)]],
                  device uint* output [[buffer(1)]], uint id [[thread_position_in_grid]]) {
    const float2 point=0.25+float2(id%256u,(id/256u)%256u)/512.0;
#if IR_SUN_QUERY_STATS
    SunQueryStats stats={};
#endif
    const float shadow=sampleCascadeShadow(point,2.0,float3(0,0,1),float3(0,0,1),
        float3(1,0,0),float3(0,1,0),float2(0),float2(1),0,buffer,128.0,true,
        float4(0,0,0,1),float3(0)
#if IR_SUN_QUERY_STATS
        ,stats
#endif
    );
#if IR_SUN_QUERY_STATS
    for(uint counter=0;counter<kSunQueryCounterCount;++counter)
        output[id*11u+1u+counter]=stats.counts[counter];
    output[id*11u]=as_type<uint>(shadow);
#else
    output[id]=as_type<uint>(shadow);
#endif
}
"""

LAUNCHER = r"""
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
int main(int argc,char** argv){@autoreleasepool{
    if(argc!=7)return 2;
    const NSUInteger queries=std::strtoul(argv[3],nullptr,10);
    const NSUInteger rounds=std::strtoul(argv[4],nullptr,10);
    const bool measure=std::strcmp(argv[5],"measure")==0;
    const double warmupSeconds=std::strtod(argv[6],nullptr);
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();
    if(!device){std::fprintf(stderr,"No Metal device\n");return 2;}
    NSError* error=nil;
    id<MTLComputePipelineState> pipelines[2],initialize=nil;
    for(int mode=0;mode<2;++mode){
        NSString* source=[NSString stringWithContentsOfFile:@(argv[mode+1])
                           encoding:NSUTF8StringEncoding error:&error];
        id<MTLLibrary> lib=[device newLibraryWithSource:source options:nil error:&error];
        if(!lib){std::fprintf(stderr,"%s\n",error.description.UTF8String);return 2;}
        pipelines[mode]=[device newComputePipelineStateWithFunction:
            [lib newFunctionWithName:@"probe"] error:&error];
        if(!pipelines[mode]){std::fprintf(stderr,"%s\n",error.description.UTF8String);return 2;}
        if(mode==0)initialize=[device newComputePipelineStateWithFunction:
            [lib newFunctionWithName:@"initialize"] error:&error];
    }
    if(!initialize)return 2;
    id<MTLBuffer> buffer=[device newBufferWithLength:BUFFER_BYTES
                          options:MTLResourceStorageModeShared];
    id<MTLBuffer> output=[device newBufferWithLength:queries*11*sizeof(uint32_t)
                          options:MTLResourceStorageModeShared];
    if(!buffer||!output)return 2;
    id<MTLCommandQueue> queue=[device newCommandQueue];
    NSMutableArray* reports=[NSMutableArray array];
    const auto start=std::chrono::steady_clock::now();
    NSUInteger warmupRounds=0, measuredRounds=0;
    double warmupElapsed=0;
    for(NSUInteger round=0;measure?measuredRounds<rounds:round<1;++round){
        const double elapsed=std::chrono::duration<double>(
            std::chrono::steady_clock::now()-start).count();
        const bool warming=measure&&(warmupRounds<2||elapsed<warmupSeconds);
        if(measure&&!warming&&measuredRounds==0)warmupElapsed=elapsed;
        for(uint32_t order=0;order<10;++order){
            const uint32_t cell=round%2?9u-order:order;
            const uint32_t count=cell<5?64u:256u, kind=cell%5;
            uint32_t config[]={count,kind};
            std::memset(buffer.contents,0xff,buffer.length);
            id<MTLCommandBuffer> setup=[queue commandBuffer];
            id<MTLComputeCommandEncoder> enc=[setup computeCommandEncoder];
            [enc setComputePipelineState:initialize];[enc setBuffer:buffer offset:0 atIndex:0];
            [enc setBytes:config length:sizeof(config) atIndex:1];
            [enc dispatchThreads:MTLSizeMake(count,1,1)
                  threadsPerThreadgroup:MTLSizeMake(MIN(count,initialize.maxTotalThreadsPerThreadgroup),1,1)];
            [enc endEncoding];[setup commit];[setup waitUntilCompleted];
            if(setup.status!=MTLCommandBufferStatusCompleted)return 3;
            for(int mode=0;mode<(round==0?2:1);++mode){
                std::memset(output.contents,0xcd,output.length);
                id<MTLCommandBuffer> command=[queue commandBuffer];
                const uint32_t dispatches=mode==0&&measure?8u:1u;
                for(uint32_t batch=0;batch<dispatches;++batch){
                    enc=[command computeCommandEncoder];
                    [enc setComputePipelineState:pipelines[mode]];
                    [enc setBuffer:buffer offset:0 atIndex:0];
                    [enc setBuffer:output offset:0 atIndex:1];
                    [enc dispatchThreads:MTLSizeMake(queries,1,1)
                          threadsPerThreadgroup:MTLSizeMake(MIN(256,pipelines[mode].maxTotalThreadsPerThreadgroup),1,1)];
                    [enc endEncoding];
                }
                [command commit];[command waitUntilCompleted];
                if(command.status!=MTLCommandBufferStatusCompleted)return 3;
                const uint32_t* words=(uint32_t*)output.contents;
                const uint32_t expectedShadow=kind>=3?0x3f800000u:0u;
                const uint32_t visits=kind==3?1u:count;
                const uint32_t misses=kind==2||kind==3?0u:(kind==4?count-1u:count);
                const uint32_t expected[]={1,1,visits,misses,0,kind>=3?1u:0u,
                    kind>=3?visits:0u,kind<3?1u:0u,0,0};
                for(NSUInteger i=0;i<queries;++i){
                    if(words[i*(mode?11:1)]!=expectedShadow){
                        std::fprintf(stderr,"shadow mismatch cell %u receiver %lu\n",cell,i);
                        return 4;
                    }
                    if(mode)for(uint32_t k=0;k<10;++k)if(words[i*11+1+k]!=expected[k]){
                        std::fprintf(stderr,"counter mismatch cell %u counter %u: %u != %u\n",
                            cell,k,words[i*11+1+k],expected[k]);return 4;
                    }
                }
                const double ms=(command.GPUEndTime-command.GPUStartTime)*1000.0/dispatches;
                if(measure&&(!std::isfinite(ms)||ms<=0)){
                    std::fprintf(stderr,"Invalid GPU timestamps\n");return 5;
                }
                if(measure&&mode==0&&!warming)[reports addObject:@{@"round":@(measuredRounds),
                    @"count":@(count),@"kind":@(kind),@"gpu_ms":@(ms),
                    @"candidate_visits_per_receiver":@(visits),@"face_misses_per_receiver":@(misses)}];
            }
        }
        if(warming)++warmupRounds;else ++measuredRounds;
    }
    NSDictionary* result=@{@"device":device.name,@"queries":@(queries),
        @"rounds":@(measure?rounds:0),@"measurement":@(measure),@"warmup_rounds":@(warmupRounds),
        @"warmup_seconds":@(measure?warmupSeconds:0),@"warmup_elapsed_seconds":@(warmupElapsed),
        @"dispatches_per_sample":@(measure?8:1),@"compile_options":@"nil (runtime default)",
        @"diagnostic_all_receivers_validated":@YES,@"samples":reports};
    NSData* json=[NSJSONSerialization dataWithJSONObject:result options:0 error:&error];
    std::fwrite(json.bytes,1,json.length,stdout);std::printf("\n");return 0;
}}
"""


def shader_source(diagnostic):
    test_helpers = str(ROOT / "scripts/tests")
    if test_helpers not in sys.path:
        sys.path.insert(0, test_helpers)
    from test_render_source_face_index import load_shader

    folder = ROOT / "engine/render/src/shaders/metal"
    return (f"#define IR_SUN_QUERY_STATS {int(diagnostic)}\n"
            + "#include <metal_stdlib>\nusing namespace metal;\n"
            + "\n".join(load_shader(folder, name, "metal") for name in (
                "ir_sun_query_stats", "ir_iso_common", "ir_sun_face_query_layout",
                "ir_sun_shadow_sample")) + KERNEL)


def buffer_bytes(source):
    import ast
    import operator
    import re

    constants = {}
    operations = {ast.Add: operator.add, ast.Mult: operator.mul, ast.Div: operator.floordiv}

    def number(node):
        if isinstance(node, ast.Constant) and type(node.value) is int:
            return node.value
        if isinstance(node, ast.Name):
            return constants[node.id]
        if isinstance(node, ast.BinOp) and type(node.op) in operations:
            return operations[type(node.op)](number(node.left), number(node.right))
        raise ValueError("unsupported layout constant")

    for name, expression in re.findall(r"constant uint (kSourceFace\w+|kSunDepthSlab\w+) = ([^;]+);", source):
        expression = re.sub(r"(\d+)u\b", r"\1", expression)
        constants[name] = number(ast.parse(expression, mode="eval").body)
    return constants["kSourceFaceBufferWords"] * 4


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--queries", type=int, default=65536)
    parser.add_argument("--rounds", type=int, default=7)
    parser.add_argument("--warmup-seconds", type=float, default=3.0,
                        help="minimum warmup duration, with at least two full fixture rounds")
    parser.add_argument("--validate-only", action="store_true",
                        help="check native outputs/counters under a GPU hold, without timings")
    args = parser.parse_args()
    if not 256 <= args.queries <= 1048576 or not 3 <= args.rounds <= 100:
        parser.error("queries must be 256..1048576; rounds must be 3..100")
    if not 0 <= args.warmup_seconds <= 30:
        parser.error("warmup-seconds must be finite and 0..30")
    if platform.system() != "Darwin":
        parser.error("native probe requires macOS Metal")
    allowed_holds = ("gpu", "benchmark") if args.validate_only else ("benchmark",)
    if (not os.environ.get("IR_RUN_LOCK_TOKEN")
            or os.environ.get("IR_ACQUIRE_HELD_VERB") not in allowed_holds):
        parser.error("run under ir-acquire benchmark -- (gpu also allowed with --validate-only)")
    compiler = shutil.which("clang++")
    if compiler is None:
        parser.error("clang++ is required")
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error("output directory must be empty to avoid stale evidence")
    output.mkdir(parents=True, exist_ok=True)
    shaders = [output / "timed.metal", output / "diagnostic.metal"]
    for diagnostic, path in enumerate(shaders):
        path.write_text(shader_source(diagnostic))
    launcher, binary = output / "native.mm", output / "native"
    launcher.write_text(LAUNCHER.replace("BUFFER_BYTES", str(buffer_bytes(shaders[0].read_text()))))
    power_before = subprocess.check_output(["pmset", "-g", "batt"], text=True).strip()
    subprocess.run([compiler, "-std=c++17", "-O2", "-fobjc-arc", str(launcher),
                    "-framework", "Metal", "-framework", "Foundation", "-o", str(binary)],
                   check=True)
    run = subprocess.run([str(binary), *map(str, shaders), str(args.queries), str(args.rounds),
                          "validate" if args.validate_only else "measure",
                          str(args.warmup_seconds)],
                         capture_output=True, text=True, timeout=240)
    (output / "native.log").write_text(run.stderr)
    if run.returncode:
        raise SystemExit(run.stderr or f"native probe failed: {run.returncode}")
    report = json.loads(run.stdout)
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    report.update(platform=platform.platform(), head=head,
                  command=sys.argv,
                  changes=subprocess.check_output(
                      ["git", "status", "--short"], cwd=ROOT, text=True),
                  compiler_version=subprocess.check_output(
                      [compiler, "--version"], text=True).strip(),
                  host_power_before=power_before,
                  host_power=subprocess.check_output(["pmset", "-g", "batt"], text=True).strip(),
                  hashes={p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in (*shaders, launcher, binary)})
    rows = []
    for count in (() if args.validate_only else (64, 256)):
        for kind, name in enumerate(CASES):
            samples = [s["gpu_ms"] for s in report["samples"]
                       if s["count"] == count and s["kind"] == kind]
            rows.append({"count": count, "case": name, "median_gpu_ms": statistics.median(samples),
                         "min_gpu_ms": min(samples), "max_gpu_ms": max(samples)})
    report["summary"] = rows
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print("Native validation passed (no timings)." if args.validate_only
          else json.dumps(rows, indent=2))


if __name__ == "__main__":
    main()
