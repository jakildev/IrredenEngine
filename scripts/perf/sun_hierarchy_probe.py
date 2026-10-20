#!/usr/bin/env python3
"""Native Metal diagnostic: GPU-built finite-quad hierarchy versus exhaustive queries.

No production routing. Validate under ir-acquire gpu; measure under ir-acquire benchmark.
"""

import argparse
import hashlib
import json
import math
import os
import platform
import shutil
import struct
import subprocess
import sys
from fractions import Fraction
from pathlib import Path

from sun_geometry_probe import Quad, load_quads

ROOT = Path(__file__).resolve().parents[2]
LEAF_SIZE = 8

KERNEL = r"""
struct Bounds { float4 low; float4 high; };
struct Config { uint records; uint leaves; uint level; uint queries; uint ordered; };

inline void readQuad(device const uint* records, uint id,
                     thread float3& corner, thread float3& u, thread float3& v) {
    const uint offset=id*10u;
    corner=as_type<float3>(uint3(records[offset],records[offset+1],records[offset+2]));
    u=as_type<float3>(uint3(records[offset+3],records[offset+4],records[offset+5]));
    v=as_type<float3>(uint3(records[offset+6],records[offset+7],records[offset+8]));
}
inline Bounds mergeBounds(Bounds a, Bounds b) {
    if(a.low.w<0)return b;
    if(b.low.w<0)return a;
    return {float4(min(a.low.xyz,b.low.xyz),max(a.low.w,b.low.w)),
            float4(max(a.high.xyz,b.high.xyz),0)};
}
kernel void buildLeaves(device const uint* records [[buffer(0)]],
                        device Bounds* tree [[buffer(1)]],
                        constant Config& cfg [[buffer(2)]],
                        device const uint* order [[buffer(5)]],
                        uint id [[thread_position_in_grid]]) {
    Bounds bounds={float4(0,0,0,-1),float4(0)};
    for(uint i=id*8u;i<min(cfg.records,(id+1u)*8u);++i){
        const uint recordId=cfg.ordered?order[2u+i*3u+2u]:i;
        float3 c,u,v;readQuad(records,recordId,c,u,v);
        float2 lowUV,highUV,lowZ,highZ;
        const float det=projectedFaceDeterminant(u.xy,v.xy);
        const bool uvSafe=sourceFaceIndexBounds(c.xy,u.xy,v.xy,det,lowUV,highUV);
        // Clamped receiver coefficients keep depth inside the same affine enclosure.
        const bool depthSafe=sourceFaceIndexBounds(float2(c.z,0),float2(u.z,0),
                                                  float2(v.z,0),0,lowZ,highZ);
        const Bounds face=uvSafe&&depthSafe
            ? Bounds{float4(lowUV,lowZ.x,0),float4(highUV,highZ.x,0)}
            : Bounds{float4(0,0,0,1),float4(0)};
        bounds=mergeBounds(bounds,face);
    }
    tree[cfg.leaves+id]=bounds;
}
kernel void buildLevel(device Bounds* tree [[buffer(1)]],
                       constant Config& cfg [[buffer(2)]],
                       uint id [[thread_position_in_grid]]) {
    const uint node=cfg.level+id;
    tree[node]=mergeBounds(tree[node*2u],tree[node*2u+1u]);
}
inline uint spreadMortonBits(uint value) {
    value=(value|(value<<16u))&0x030000FFu;
    value=(value|(value<<8u))&0x0300F00Fu;
    value=(value|(value<<4u))&0x030C30C3u;
    return (value|(value<<2u))&0x09249249u;
}
kernel void spatialKeys(device const uint* records [[buffer(0)]],
                        device const Bounds* tree [[buffer(1)]],
                        constant Config& cfg [[buffer(2)]],
                        device uint* order [[buffer(5)]],
                        uint id [[thread_position_in_grid]]) {
    if(id==0){order[0]=0;order[1]=cfg.records;}
    if(id>=cfg.records)return;
    float3 c,u,v;readQuad(records,id,c,u,v);
    const float3 center=c+0.5*u+0.5*v;
    uint3 coordinates=uint3(0);
    for(uint axis=0;axis<3;++axis){
        const float span=tree[1].high[axis]-tree[1].low[axis];
        if(tree[1].low.w!=0||!isfinite(span)||span<=0)continue;
        const float position=(center[axis]-tree[1].low[axis])/span;
        if(isfinite(position))coordinates[axis]=uint(clamp(position,0.0,1.0)*1023.0);
    }
    const uint key=spreadMortonBits(coordinates.x)
                  |(spreadMortonBits(coordinates.y)<<1u)
                  |(spreadMortonBits(coordinates.z)<<2u);
    // The shared sorter compares (word0 & 0x7fffffff, word2, word1, word0's high bit).
    // Thirty-bit spatial keys and record IDs fit that ordering without changing geometry.
    order[2u+id*3u]=key;order[3u+id*3u]=0u;order[4u+id*3u]=id;
}

inline bool overlaps(Bounds bounds,float4 point,float throwDistance) {
    if(bounds.low.w<0)return false;
    // Unsupported arithmetic cannot certify a miss, including in parent nodes.
    if(bounds.low.w>0)return true;
    return all(point.xy>=bounds.low.xy)&&all(point.xy<=bounds.high.xy)
        &&sourceFaceBoundUp(point.z-bounds.low.z)>point.w
        &&sourceFaceBoundDown(point.z-bounds.high.z)<throwDistance;
}
inline bool blocks(device const uint* records,uint id,float4 point,float throwDistance){
    float3 c,u,v;readQuad(records,id,c,u,v);
    const float separation=sourceFaceRaySeparation(point.xy,point.z,c,u,v);
    return separation>point.w&&separation<throwDistance;
}
kernel void query(device const uint* records [[buffer(0)]],
                  device const Bounds* tree [[buffer(1)]],
                  constant Config& cfg [[buffer(2)]],
                  device const float4* points [[buffer(3)]],
                  device uint4* output [[buffer(4)]],
                  device const uint* order [[buffer(5)]],
                  uint id [[thread_position_in_grid]]) {
    const float4 point=points[id];
    bool reference=false,actual=false;
    uint visited=0,tested=0,node=1;
#if IR_HIERARCHY_DIAGNOSTIC
    for(uint i=0;i<cfg.records;++i)if(blocks(records,i,point,64.0)){reference=true;break;}
#endif
    while(true){
#if IR_HIERARCHY_DIAGNOSTIC
        ++visited;
#endif
        if(overlaps(tree[node],point,64.0)){
            if(node<cfg.leaves){node*=2u;continue;}
            const uint first=(node-cfg.leaves)*8u;
            for(uint i=first;i<min(cfg.records,first+8u);++i){
#if IR_HIERARCHY_DIAGNOSTIC
                ++tested;
#endif
                const uint recordId=cfg.ordered?order[2u+i*3u+2u]:i;
                if(blocks(records,recordId,point,64.0)){actual=true;break;}
            }
            if(actual)break;
        }
        // Ascend past completed right children, then visit the next right sibling.
        while(node>1u&&(node&1u)!=0u)node/=2u;
        if(node==1u)break;
        ++node;
    }
#if IR_HIERARCHY_DIAGNOSTIC
    output[id]=uint4(actual,reference,visited,tested);
#else
    output[id]=uint4(actual,0,0,0);
#endif
}
"""

LAUNCHER = r"""
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <vector>
int main(int argc,char** argv){@autoreleasepool{
    if(argc!=9)return 2;
    const uint32_t rounds=std::strtoul(argv[7],nullptr,10);
    const uint32_t dispatches=std::strtoul(argv[8],nullptr,10);
    if(rounds>100||dispatches<1||dispatches>64)return 2;
    const bool spatial=std::strcmp(argv[5],"spatial")==0;
    NSError* error=nil;
    NSData* input=[NSData dataWithContentsOfFile:@(argv[2])];
    if(!input||input.length<16)return 2;
    uint32_t cfg[5]={};std::memcpy(cfg,input.bytes,16);
    uint32_t leaves=1;while(leaves<(cfg[0]+7u)/8u)leaves*=2;
    if(cfg[0]>65536||cfg[1]!=leaves||cfg[3]==0)return 2;
    if(input.length!=16+size_t(cfg[0])*40+size_t(cfg[3])*16)return 2;
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();
    if(!device){std::fprintf(stderr,"No Metal device\n");return 2;}
    NSString* source=[NSString stringWithContentsOfFile:@(argv[1])
                       encoding:NSUTF8StringEncoding error:&error];
    MTLCompileOptions* options=[MTLCompileOptions new];
    options.mathMode=std::strcmp(argv[4],"fast")==0?MTLMathModeFast:MTLMathModeSafe;
    id<MTLLibrary> lib=[device newLibraryWithSource:source options:options error:&error];
    if(!lib){std::fprintf(stderr,"%s\n",error.description.UTF8String);return 2;}
    NSArray* names=@[@"buildLeaves",@"buildLevel",@"query",
                     @"spatialKeys",@"c_per_axis_overflow_sort"];
    id<MTLComputePipelineState> pipelines[5];
    for(int i=0;i<(spatial?5:3);++i){
        pipelines[i]=[device newComputePipelineStateWithFunction:
            [lib newFunctionWithName:names[i]] error:&error];
        if(!pipelines[i]){std::fprintf(stderr,"%s\n",error.description.UTF8String);return 2;}
    }
    id<MTLBuffer> records=[device newBufferWithLength:MAX(4,size_t(cfg[0])*40)
                          options:MTLResourceStorageModeShared];
    id<MTLBuffer> points=[device newBufferWithLength:size_t(cfg[3])*16
                         options:MTLResourceStorageModeShared];
    id<MTLBuffer> tree=[device newBufferWithLength:size_t(cfg[1])*2*32
                       options:MTLResourceStorageModeShared];
    id<MTLBuffer> output=[device newBufferWithLength:size_t(cfg[3])*16
                         options:MTLResourceStorageModeShared];
    const uint32_t sortSpan=MAX(2048,cfg[1]*8);
    id<MTLBuffer> order=[device newBufferWithLength:(2+size_t(sortSpan)*3)*4
                        options:MTLResourceStorageModeShared];
    id<MTLBuffer> sortArgs=[device newBufferWithLength:21*16
                           options:MTLResourceStorageModeShared];
    if(!records||!points||!tree||!output||!order||!sortArgs)return 2;
    std::memcpy(records.contents,(const char*)input.bytes+16,size_t(cfg[0])*40);
    std::memcpy(points.contents,(const char*)input.bytes+16+size_t(cfg[0])*40,points.length);
    std::memset(tree.contents,0xcd,tree.length);
    std::memset(output.contents,0xcd,output.length);
    id<MTLCommandQueue> queue=[device newCommandQueue];
    id<MTLCommandBuffer> command=[queue commandBuffer];
    auto encode=[&](uint32_t phase,uint32_t count){
        id<MTLComputeCommandEncoder> enc=[command computeCommandEncoder];
        [enc setComputePipelineState:pipelines[phase]];
        [enc setBuffer:records offset:0 atIndex:0];[enc setBuffer:tree offset:0 atIndex:1];
        [enc setBytes:cfg length:sizeof(cfg) atIndex:2];[enc setBuffer:points offset:0 atIndex:3];
        [enc setBuffer:output offset:0 atIndex:4];[enc setBuffer:order offset:0 atIndex:5];
        [enc dispatchThreads:MTLSizeMake(count,1,1) threadsPerThreadgroup:
            MTLSizeMake(MIN(count,MIN(256,pipelines[phase].maxTotalThreadsPerThreadgroup)),1,1)];
        [enc endEncoding];
    };
    auto buildBounds=[&](){
        encode(0,cfg[1]);
        // Separate encoders publish each level before its parents consume it.
        for(cfg[2]=cfg[1]/2;cfg[2];cfg[2]/=2)encode(1,cfg[2]);
    };
    auto build=[&](){
    cfg[4]=0;
    buildBounds();
    if(spatial){
        encode(3,MAX(1,cfg[0]));
        auto sort=[&](uint32_t mode,uint32_t k,uint32_t low,uint32_t high){
            const float frame[]={float(mode),float(k),float(low),float(high),
                                 0,0,2,float(sortSpan)};
            id<MTLComputeCommandEncoder> enc=[command computeCommandEncoder];
            [enc setComputePipelineState:pipelines[4]];
            [enc setBytes:frame length:sizeof(frame) atIndex:7];
            [enc setBuffer:order offset:0 atIndex:28];
            [enc setBuffer:sortArgs offset:0 atIndex:26];
            const uint32_t groups=sortSpan/(mode==0?256:2048);
            [enc dispatchThreadgroups:MTLSizeMake(groups,1,1)
                 threadsPerThreadgroup:MTLSizeMake(256,1,1)];
            [enc endEncoding];
        };
        sort(0,0,0,0);sort(1,0,0,0);
        for(uint32_t stage=12;(1u<<stage)<=sortSpan;++stage){
            for(uint32_t remaining=stage;remaining;){
                const uint32_t width=MIN(remaining,11u);
                sort(2,1u<<stage,remaining-width,remaining-1);
                remaining-=width;
            }
        }
        cfg[4]=1;
        buildBounds();
    }
    };
    build();
    encode(2,cfg[3]);
    [command commit];[command waitUntilCompleted];
    if(command.status!=MTLCommandBufferStatusCompleted){
        std::fprintf(stderr,"%s\n",command.error.description.UTF8String);return 3;
    }
    if(![[NSData dataWithBytes:output.contents length:output.length]
          writeToFile:@(argv[3]) options:0 error:&error])return 4;
    NSString* boundsPath=[@(argv[3]) stringByAppendingString:@".bounds"];
    if(![[NSData dataWithBytes:tree.contents length:tree.length]
          writeToFile:boundsPath options:0 error:&error])return 4;
    if(spatial&&![[NSData dataWithBytes:order.contents length:order.length]
          writeToFile:[@(argv[3]) stringByAppendingString:@".order"]
          options:0 error:&error])return 4;
    if(rounds){
        std::vector<uint32_t> expected(cfg[3]);
        for(uint32_t i=0;i<cfg[3];++i)expected[i]=((uint32_t*)output.contents)[i*4];
        NSString* timedSource=[NSString stringWithContentsOfFile:@(argv[6])
                               encoding:NSUTF8StringEncoding error:&error];
        id<MTLLibrary> timedLib=[device newLibraryWithSource:timedSource
                               options:options error:&error];
        if(!timedLib){std::fprintf(stderr,"%s\n",error.description.UTF8String);return 2;}
        pipelines[2]=[device newComputePipelineStateWithFunction:
            [timedLib newFunctionWithName:@"query"] error:&error];
        if(!pipelines[2])return 2;
        NSMutableArray* samples=[NSMutableArray array];
        const auto start=std::chrono::steady_clock::now();
        uint32_t warmup=0,measured=0;
        while(measured<rounds){@autoreleasepool{
            const bool warming=warmup<2||std::chrono::duration<double>(
                std::chrono::steady_clock::now()-start).count()<3.0;
            command=[queue commandBuffer];
            for(uint32_t i=0;i<dispatches;++i)build();
            [command commit];[command waitUntilCompleted];
            if(command.status!=MTLCommandBufferStatusCompleted)return 3;
            const double buildMs=(command.GPUEndTime-command.GPUStartTime)*1000/dispatches;
            command=[queue commandBuffer];
            for(uint32_t i=0;i<dispatches;++i)encode(2,cfg[3]);
            [command commit];[command waitUntilCompleted];
            if(command.status!=MTLCommandBufferStatusCompleted)return 3;
            const double queryMs=(command.GPUEndTime-command.GPUStartTime)*1000/dispatches;
            if(!std::isfinite(buildMs)||!std::isfinite(queryMs)||buildMs<=0||queryMs<=0)return 5;
            const uint32_t* actual=(const uint32_t*)output.contents;
            for(uint32_t i=0;i<cfg[3];++i)if(actual[i*4]!=expected[i])return 6;
            if(warming){++warmup;continue;}
            [samples addObject:@{@"round":@(measured++),@"build_gpu_ms":@(buildMs),
                                @"query_gpu_ms":@(queryMs)}];
        }
        }
        NSData* json=[NSJSONSerialization dataWithJSONObject:
            @{@"warmup_rounds":@(warmup),@"minimum_warmup_seconds":@3,
              @"dispatches_per_sample":@(dispatches),@"samples":samples}
            options:0 error:&error];
        if(![json writeToFile:[@(argv[3]) stringByAppendingString:@".timings.json"]
                   options:0 error:&error])return 4;
    }
    std::printf("%s\n",device.name.UTF8String);return 0;
}}
"""


def shader_source(spatial=False, diagnostic=True):
    sys.path.insert(0, str(ROOT / "scripts/tests"))
    from test_render_source_face_index import load_shader

    folder = ROOT / "engine/render/src/shaders/metal"
    sorter = ""
    if spatial:
        sorter = load_shader(folder, "c_per_axis_overflow_sort", "metal")
        parameter = "constant FrameDataVoxelToTrixel &frameData"
        if sorter.count(parameter) != 1:
            raise ValueError("shared sorter frame adapter no longer matches")
        sorter = ("struct HierarchySortConfig { float4 overflowSortStep; "
                  "float4 overflowScratchLayout; };\n"
                  + sorter.replace(parameter, "constant HierarchySortConfig &frameData"))
    return (f"#define IR_HIERARCHY_DIAGNOSTIC {int(diagnostic)}\n"
            + "#define IR_SUN_QUERY_STATS 0\n#include <metal_stdlib>\nusing namespace metal;\n"
            + "\n".join(load_shader(folder, name, "metal") for name in (
                "ir_sun_face_bounds", "ir_sun_face_query_layout")) + KERNEL + sorter)


def pack_input(quads, points):
    if len(quads) > 65536 or not points:
        raise ValueError("require at most 65536 records and at least one query")
    leaves = 1 << (max(1, (len(quads) + LEAF_SIZE - 1) // LEAF_SIZE) - 1).bit_length()
    data = bytearray(struct.pack("<4I", len(quads), leaves, 0, len(points)))
    for face in quads:
        data.extend(struct.pack("<9fI", *face.corner, *face.edge_u, *face.edge_v, 0))
    for point in points:
        data.extend(struct.pack("<4f", *point, 1 / 256))
    return data, leaves


def validate_output(raw, bounds, quads, points, leaves, order=None):
    if len(raw) != len(points) * 16 or len(bounds) != leaves * 2 * 32:
        raise ValueError("incomplete native output")
    ordered_quads = quads if order is None else [quads[index] for index in order]
    nodes = list(struct.iter_unpack("<8f", bounds))
    for node in range(1, 2 * leaves):
        row = nodes[node]
        if row[3] not in (-1, 0, 1):
            raise ValueError("unpublished node")
        if row[3] == 0 and (not all(math.isfinite(v) for v in row)
                             or any(row[a] > row[a + 4] for a in range(3))):
            raise ValueError("invalid bounded node")
        if node < leaves:
            children = [nodes[node * 2], nodes[node * 2 + 1]]
            if (all(child[3] == -1 for child in children)) != (row[3] == -1):
                raise ValueError("incorrect empty parent")
            if any(child[3] == 1 for child in children) and row[3] != 1:
                raise ValueError("unbounded child was discarded")
            for child in children:
                if child[3] == 0 and row[3] != 1:
                    if row[3] == -1 or any(row[a] > child[a] or row[a + 4] < child[a + 4]
                                           for a in range(3)):
                        raise ValueError("parent does not enclose child")
        elif row[3] != 1:
            faces = ordered_quads[(node - leaves) * LEAF_SIZE:(node - leaves + 1) * LEAF_SIZE]
            if (not faces) != (row[3] == -1):
                raise ValueError("incorrect empty leaf")
            for face in faces:
                if any(Fraction(row[a]) > face.bounds[a][0]
                       or Fraction(row[a + 4]) < face.bounds[a][1] for a in range(3)):
                    raise ValueError("GPU bounds do not enclose exact quad")
    rows = list(struct.iter_unpack("<4I", raw))
    if any(actual != reference or actual > 1 or not 1 <= visited < 2 * leaves
           or tested > len(quads) for actual, reference, visited, tested in rows):
        raise ValueError("GPU hierarchy disagrees with exhaustive query or counters are invalid")
    return {"queries": len(rows), "blocked": sum(row[0] for row in rows),
            "mismatches": 0, "records": len(quads), "leaves": leaves,
            "tree_bytes": len(bounds), "coverage": [bool(row[0]) for row in rows],
            "node_visits": {"mean": sum(r[2] for r in rows) / len(rows),
                            "max": max(r[2] for r in rows)},
            "quad_tests": {"mean": sum(r[3] for r in rows) / len(rows),
                           "max": max(r[3] for r in rows)}}


def control_cases(spatial=False):
    def face(corner=(0, 0, 0), u=(1, 0, 0), v=(0, 1, 0)):
        return Quad(*(tuple(map(Fraction, values)) for values in (corner, u, v)))

    center = [(Fraction(1, 2), Fraction(1, 2), Fraction(10))]
    lower, distant = face((0, 0, 2)), face((0, 0, -120))
    next_bias = struct.unpack("<f", struct.pack("<I", 0x3b800001))[0]
    below_throw = struct.unpack("<f", struct.pack("<I", 0x427fffff))[0]
    cases = [
        ("empty", [], center, [False]),
        ("lower-first", [lower] + [distant] * 512, center, [True]),
        ("lower-last", [distant] * 512 + [lower], center, [True]),
        ("distant-only", [distant] * 512, center, [False]),
        ("capacity", [distant] * 65535 + [lower], center, [True]),
        ("footprint-miss", [face(u=(10, 10, 0), v=(1, 0, 0))] * 512,
         [(Fraction(1), Fraction(8), Fraction(10))], [False]),
        ("boundaries", [face()], [(Fraction(0), Fraction(0), Fraction(1)),
                                  (Fraction(1), Fraction(1), Fraction(1))]
         + [(Fraction(1, 2), Fraction(1, 2), Fraction(z))
            for z in (Fraction(1, 256), 64, next_bias, below_throw)],
         [True, True, False, False, True, True]),
        ("subnormal", [face((Fraction(1, 2**149), 0, 0))], center, [True]),
        ("reversed-face", [face((1, 1, 2), (-1, 0, 0), (0, -1, 0))], center, [True]),
    ]
    if spatial:
        corners = [((i & 1) * 8, ((i >> 1) & 1) * 8, ((i >> 2) & 1) * 8)
                   for i in (7, 0, 5, 2, 6, 1, 4, 3)]
        cases.append(("spatial-octants", [face(c) for c in corners], center, [True]))
    return cases


def validate_order(raw, count):
    span = max(2048, 1 << (max(count, 1) - 1).bit_length())
    if len(raw) != (2 + span * 3) * 4:
        raise ValueError("incomplete order readback")
    if struct.unpack_from("<2I", raw) != (0, count):
        raise ValueError("incorrect sort header")
    records = list(struct.iter_unpack("<3I", raw[8:]))
    active = records[:count]
    if (any(key >= 1 << 30 or unused != 0 for key, unused, _ in active)
            or sorted(index for _, _, index in active) != list(range(count))):
        raise ValueError("spatial order is not a lossless record permutation")
    if active != sorted(active, key=lambda row: (row[0], row[2])):
        raise ValueError("spatial keys are not sorted")
    if any(row != (0xffffffff,) * 3 for row in records[count:]):
        raise ValueError("sort padding is not sentinel-filled")
    return [row[2] for row in active]


def validate_octant_order(order):
    # Separated octants have distinct Morton keys; their input record IDs are scrambled.
    if order != [1, 5, 3, 7, 6, 2, 4, 0]:
        raise ValueError("spatial octants do not follow XYZ Morton order")


def run_case(binary, shader, output, name, quads, points, expected=None, spatial=False,
             rounds=0, dispatches=8):
    data, leaves = pack_input(quads, points)
    input_path = output / f"{name}.input.bin"
    input_path.write_bytes(data)
    results = {}
    for mode in ("fast", "precise"):
        result_path = output / f"{name}.{mode}.bin"
        run = subprocess.run([str(binary), str(shader), str(input_path), str(result_path), mode,
                              "spatial" if spatial else "unsorted",
                              str(output / "timed.metal"), str(rounds), str(dispatches)],
                             capture_output=True, text=True, timeout=120)
        (output / f"{name}.{mode}.log").write_text(run.stdout + run.stderr)
        if run.returncode:
            raise RuntimeError(run.stderr or f"native probe failed: {run.returncode}")
        order = (validate_order(Path(str(result_path) + ".order").read_bytes(), len(quads))
                 if spatial else None)
        if name == "spatial-octants":
            validate_octant_order(order)
        result = validate_output(result_path.read_bytes(),
                                 Path(str(result_path) + ".bounds").read_bytes(),
                                 quads, points, leaves, order)
        if expected is not None and result["coverage"] != expected:
            raise ValueError(f"{name}/{mode} disagrees with independent expected visibility")
        if name == "footprint-miss" and result["quad_tests"]["max"] != len(quads):
            raise ValueError("worst-case control did not visit every candidate")
        results[mode] = result
    timing = ({mode: json.loads((output / f"{name}.{mode}.bin.timings.json").read_text())
               for mode in ("fast", "precise")} if rounds else None)
    return {"device": run.stdout.strip(), "results": results, "timing": timing,
            "input_sha256": hashlib.sha256(data).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", nargs="?", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--spatial", action="store_true", help="GPU Morton keys and shared sorter")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--measure", action="store_true",
                        help="separate build/query GPU timestamps")
    parser.add_argument("--rounds", type=int, default=7)
    parser.add_argument("--dispatches", type=int, default=8,
                        help="build/query dispatch batches per timing sample")
    parser.add_argument("--repeat-queries", type=int, default=1,
                        help="repeat the 17x17 receiver grid to expose query throughput")
    args = parser.parse_args()
    if not 3 <= args.rounds <= 100 or not 1 <= args.repeat_queries <= 1024:
        parser.error("rounds must be 3..100; repeat-queries must be 1..1024")
    if not 1 <= args.dispatches <= 64:
        parser.error("dispatches must be 1..64")
    if args.self_test and (args.measure or args.repeat_queries != 1):
        parser.error("synthetic controls are validation-only without query repetition")
    if (args.capture is None) == (not args.self_test):
        parser.error("supply a capture or --self-test, exclusively")
    if platform.system() != "Darwin":
        parser.error("native probe requires macOS Metal")
    allowed = ("benchmark",) if args.measure else ("gpu", "benchmark")
    if (not os.environ.get("IR_RUN_LOCK_TOKEN")
            or os.environ.get("IR_ACQUIRE_HELD_VERB") not in allowed):
        parser.error("run under ir-acquire benchmark -- (gpu allowed for validation)")
    cases = control_cases(args.spatial) if args.self_test else []
    if args.capture:
        quads, _ = load_quads(args.capture)
        points = [(Fraction(-12) + Fraction(3, 2) * x, Fraction(-12) + Fraction(3, 2) * y,
                   Fraction(2)) for y in range(17) for x in range(17)]
        points *= args.repeat_queries
        cases = [("captured", quads, points, None),
                 ("reversed", list(reversed(quads)), points, None)]
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error("output directory must be empty")
    output.mkdir(parents=True, exist_ok=True)
    shader, launcher, binary = (output / name for name in ("probe.metal", "native.mm", "native"))
    shader.write_text(shader_source(args.spatial))
    timed_shader = output / "timed.metal"
    timed_shader.write_text(shader_source(args.spatial, diagnostic=False))
    launcher.write_text(LAUNCHER)
    power_before = subprocess.check_output(["pmset", "-g", "batt"], text=True).strip()
    compiler = shutil.which("clang++")
    if not compiler:
        parser.error("clang++ is required")
    subprocess.run([compiler, "-std=c++17", "-O2", "-fobjc-arc", str(launcher),
                    "-framework", "Metal", "-framework", "Foundation", "-o", str(binary)],
                   check=True)
    results = {name: run_case(binary, shader, output, name, quads, points, expected, args.spatial,
                              args.rounds if args.measure else 0, args.dispatches)
               for name, quads, points, expected in cases}
    if args.capture:
        for mode in ("fast", "precise"):
            if (results["captured"]["results"][mode]["coverage"]
                    != results["reversed"]["results"][mode]["coverage"]):
                raise ValueError("record order changed visibility")
    report = {"capture": str(args.capture) if args.capture else None,
              "capture_sha256": hashlib.sha256(args.capture.read_bytes()).hexdigest()
              if args.capture else None,
              "platform": platform.platform(), "cases": results,
              "head": subprocess.check_output(
                  ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
              "changes": subprocess.check_output(["git", "status", "--short"], cwd=ROOT, text=True),
              "hashes": {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in (shader, timed_shader, launcher, binary, Path(__file__))},
              "compiler": subprocess.check_output([compiler, "--version"], text=True).strip(),
              "command": sys.argv,
              "spatial": args.spatial, "measurement": args.measure,
              "repeat_queries": args.repeat_queries, "power_before": power_before,
              "power_after": subprocess.check_output(["pmset", "-g", "batt"], text=True).strip(),
              "scope": "GPU hierarchy microbenchmark; excludes frame and geometry production"}
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Native hierarchy validation passed: {len(cases)} cases, both math modes")


if __name__ == "__main__":
    main()
