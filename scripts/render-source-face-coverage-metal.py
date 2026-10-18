"""Manually validate native Metal face-query/index-bound containment.

Run with: ir-acquire gpu -- python3 scripts/render-source-face-coverage-metal.py
--output /tmp/source-face-metal. This exercises native default compilation and
tile containment, not atomic publication, rasterization, or rendered images.
"""

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

LAUNCHER = r"""
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cstdint>
#include <cstdio>
int main(int argc,char** argv){@autoreleasepool{
    if(argc!=3)return 2;
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();
    if(!device){std::fprintf(stderr,"No Metal device\n");return 2;}
    NSError* error=nil;
    NSString* source=[NSString stringWithContentsOfFile:@(argv[1])
                       encoding:NSUTF8StringEncoding error:&error];
    id<MTLLibrary> library=[device newLibraryWithSource:source options:nil error:&error];
    if(!library){std::fprintf(stderr,"%s\n",error.description.UTF8String);return 2;}
    id<MTLComputePipelineState> pipeline=[device newComputePipelineStateWithFunction:
        [library newFunctionWithName:@"coverage"] error:&error];
    if(!pipeline){std::fprintf(stderr,"%s\n",error.description.UTF8String);return 2;}
    NSData* fixtures=[NSData dataWithContentsOfFile:@(argv[2])];
    if(!fixtures.length||fixtures.length%128)return 2;
    const NSUInteger count=fixtures.length/128;
    id<MTLBuffer> inputs=[device newBufferWithBytes:fixtures.bytes length:fixtures.length
                         options:MTLResourceStorageModeShared];
    id<MTLBuffer> results=[device newBufferWithLength:6*sizeof(uint32_t)
                          options:MTLResourceStorageModeShared];
    uint32_t* words=(uint32_t*)results.contents;
    for(int i=0;i<6;++i)words[i]=0;
    words[5]=UINT32_MAX;
    id<MTLCommandQueue> queue=[device newCommandQueue];
    id<MTLCommandBuffer> command=[queue commandBuffer];
    id<MTLComputeCommandEncoder> encoder=[command computeCommandEncoder];
    [encoder setComputePipelineState:pipeline];
    [encoder setBuffer:inputs offset:0 atIndex:0];
    [encoder setBuffer:results offset:0 atIndex:1];
    [encoder dispatchThreads:MTLSizeMake(count,1,1)
          threadsPerThreadgroup:MTLSizeMake(MIN(256,pipeline.maxTotalThreadsPerThreadgroup),1,1)];
    [encoder endEncoding];[command commit];[command waitUntilCompleted];
    if(command.status!=MTLCommandBufferStatusCompleted){
        std::fprintf(stderr,"%s\n",command.error.description.UTF8String);return 2;
    }
    NSDictionary* report=@{@"device":device.name,@"fixture_samples":@(count),
        @"cascade_queries":@(words[0]),@"accepted_in_map":@(words[1]),
        @"rejected":@(words[2]),@"outside":@(words[3]),@"failures":@(words[4]),
        @"first_failure_query":@(words[5]),@"compile_options":@"nil (runtime default)",
        @"validation":@"native Metal predicate and index-bound containment"};
    NSData* json=[NSJSONSerialization dataWithJSONObject:report options:0 error:&error];
    std::fwrite(json.bytes,1,json.length,stdout);std::printf("\n");
    return 0;
}}
"""

KERNEL = r"""
struct Fixture {float4 corner,edgeU,edgeV,point,origin0,origin1,texel0,texel1;};
kernel void coverage(device const Fixture* inputs [[buffer(0)]],
                     device atomic_uint* result [[buffer(1)]],uint id [[thread_position_in_grid]]){
    const Fixture f=inputs[id];
    for(uint cascade=0;cascade<2;++cascade){
        atomic_fetch_add_explicit(&result[0],1u,memory_order_relaxed);
        const float separation=sourceFaceRaySeparation(f.point.xy,2.0,
                                                       f.corner.xyz,f.edgeU.xyz,f.edgeV.xyz);
        if(separation!=2.0){
            atomic_fetch_add_explicit(&result[2],1u,memory_order_relaxed);continue;
        }
        const float2 sunUV=f.point.xy;
        const float2 origin=cascade==0?f.origin0.xy:f.origin1.xy;
        const float2 texelSz=cascade==0?f.texel0.xy:f.texel1.xy;
        QUERY_TILE
        if(any(tile<0)||any(tile>=int(kSourceFaceTilesPerAxis))){
            atomic_fetch_add_explicit(&result[3],1u,memory_order_relaxed);continue;
        }
        atomic_fetch_add_explicit(&result[1],1u,memory_order_relaxed);
        if(!indexedTile(f.corner.xyz,f.edgeU.xyz,f.edgeV.xyz,origin,texelSz,tile)){
            atomic_fetch_add_explicit(&result[4],1u,memory_order_relaxed);
            atomic_fetch_min_explicit(&result[5],id*2+cascade,memory_order_relaxed);
        }
    }
}
"""


def fixture_source():
    from shader_contract_helpers import extract_block
    from test_render_source_face_index_coverage import CASES, host_source

    cases = CASES.replace("CALL_INDEX", "")
    cases = cases.replace(extract_block(cases, "void reset(bool exhausted)"), "void reset(bool){}")
    exporter = r"""
void query(vec3 c,vec3 u,vec3 v,vec2 p,bool){
    const float values[]={c.x,c.y,c.z,0,u.x,u.y,u.z,0,v.x,v.y,v.z,0,p.x,p.y,0,0,
        cascadeOriginUV_0.x,cascadeOriginUV_0.y,0,0,cascadeOriginUV_1.x,cascadeOriginUV_1.y,0,0,
        cascadeTexelSize_0.x,cascadeTexelSize_0.y,0,0,cascadeTexelSize_1.x,cascadeTexelSize_1.y,0,0};
    std::fwrite(values,sizeof(values),1,stdout);
}
"""
    cases = cases.replace(extract_block(cases, "void query("), exporter)
    cases = cases[:cases.index("    if(accepted<10000")] + "return 0;}\n"
    return "#include <cstdio>\n" + host_source("metal", cases=cases)


def shader_source(raw_bounds):
    from test_render_source_face_index import load_shader

    folder = ROOT / "engine/render/src/shaders/metal"
    indexer = (folder / "ir_sun_face_index.metal").read_text()
    start = indexer.index("    const float determinant",
                          indexer.index("inline void indexSourceSunFace"))
    bounds = indexer[start:indexer.index("#ifdef IR_SUN_FACE_INDEX_COOPERATIVE", start)]
    bounds = bounds.replace("return;", "return false;")
    if raw_bounds:
        bounds = """
    const float2 low=min(min(corner.xy,corner.xy+edgeU.xy),
                        min(corner.xy+edgeV.xy,corner.xy+edgeU.xy+edgeV.xy));
    const float2 high=max(max(corner.xy,corner.xy+edgeU.xy),
                         max(corner.xy+edgeV.xy,corner.xy+edgeU.xy+edgeV.xy));
"""
    start = indexer.index("        const bool fullCascade")
    mapping = indexer[start:indexer.index("        if (first.x > last.x", start)]
    sampler = (folder / "ir_sun_shadow_sample.metal").read_text()
    tile = re.search(r"const int2 tile = [^;]+;", sampler)
    if tile is None:
        raise ValueError("Missing native receiver tile expression")
    return ("#include <metal_stdlib>\nusing namespace metal;\n"
            + load_shader(folder, "ir_sun_face_query_layout", "metal")
            + load_shader(folder, "ir_sun_face_bounds", "metal")
            + "\nbool indexedTile(float3 corner,float3 edgeU,float3 edgeV,"
            "float2 origin,float2 texelSz,int2 tile){\n" + bounds
            + "const uint type=0u;const float2 cellSize=texelSz*float(kSourceFaceTileEdge);\n"
            + mapping + "return all(tile>=first)&&all(tile<=last);}\n"
            + KERNEL.replace("QUERY_TILE", tile[0]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if platform.system() != "Darwin":
        raise SystemExit("This manual native validator requires macOS Metal")
    if (not os.environ.get("IR_RUN_LOCK_TOKEN")
            or os.environ.get("IR_ACQUIRE_HELD_VERB") not in ("gpu", "benchmark")):
        raise SystemExit("Run this validator under ir-acquire gpu -- to hold the GPU lease")
    compiler = shutil.which("clang++")
    if compiler is None:
        raise SystemExit("clang++ is required")
    sys.path.insert(0, str(ROOT / "scripts/tests"))
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    exporter, launcher = output / "fixtures.cpp", output / "native.mm"
    exporter.write_text(fixture_source())
    launcher.write_text(LAUNCHER)
    exporter_binary, native_binary = output / "fixtures", output / "native"
    subprocess.run([compiler, "-std=c++17", "-O2", "-ffp-contract=off", str(exporter),
                    "-o", str(exporter_binary)], check=True)
    subprocess.run([compiler, "-std=c++17", "-O2", "-fobjc-arc", str(launcher),
                    "-framework", "Metal", "-framework", "Foundation", "-o", str(native_binary)],
                   check=True)
    fixtures = output / "fixtures.bin"
    with fixtures.open("wb") as stream:
        subprocess.run([str(exporter_binary)], stdout=stream, check=True)
    reports = []
    for raw_bounds in (False, True):
        name = "raw-aabb-mutation" if raw_bounds else "production"
        shader = output / f"{name}.metal"
        shader.write_text(shader_source(raw_bounds))
        run = subprocess.run([str(native_binary), str(shader), str(fixtures)],
                             capture_output=True, text=True, timeout=120)
        if run.returncode:
            raise SystemExit(run.stderr)
        report = json.loads(run.stdout)
        report.update(variant=name, shader_sha256=hashlib.sha256(shader.read_bytes()).hexdigest())
        reports.append(report)
        print(json.dumps(report), flush=True)
    metadata = {"platform": platform.platform(),
                "fixture_sha256": hashlib.sha256(fixtures.read_bytes()).hexdigest(),
                "launcher_source_sha256": hashlib.sha256(launcher.read_bytes()).hexdigest(),
                "launcher_sha256":
                hashlib.sha256(native_binary.read_bytes()).hexdigest(), "reports": reports}
    (output / "results.json").write_text(json.dumps(metadata, indent=2) + "\n")
    if (reports[0]["accepted_in_map"] < 10000 or reports[0]["failures"] != 0
            or reports[1]["failures"] == 0):
        raise SystemExit("Native containment or raw-AABB positive control failed; see results.json")


if __name__ == "__main__":
    main()
