#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cmath>
#include <fstream>
#include <set>
#include <array>
#include <vector>
#include <iostream>

struct Vertex { float position[4]; float color[4]; };
int main(int argc, char** argv) { @autoreleasepool {
    if (argc != 3) return 2;
    NSError* error = nil;
    NSData* input = [NSData dataWithContentsOfFile:@(argv[1])];
    NSDictionary* spec = [NSJSONSerialization JSONObjectWithData:input options:0 error:&error];
    if (!spec) { NSLog(@"input: %@", error); return 2; }
    std::set<std::array<int,3>> cells;
    for (NSArray* cell in spec[@"cells"]) cells.insert({[cell[0] intValue],[cell[1] intValue],[cell[2] intValue]});
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) return 3;
    NSString* source = @"#include <metal_stdlib>\nusing namespace metal;\n"
        "struct V { float4 p; float4 c; }; struct O { float4 p [[position]]; float4 c [[flat]]; };\n"
        "vertex O vertexMain(uint i [[vertex_id]], device const V* v [[buffer(0)]]) { float4 w=v[i].p; float c=cos(w.w), s=sin(w.w); float vx=c*w.x+s*w.y, vy=-s*w.x+c*w.y; return {float4(8*(-vx+vy)/640,-4*(-vx-vy+2*w.z)/360,(vx+vy+w.z+64)/128,1),v[i].c}; }\n"
        "fragment float4 fragmentMain(O in [[stage_in]]) { return in.c; }\n";
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
    if (!library) { NSLog(@"library: %@",error); return 3; }
    MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [library newFunctionWithName:@"vertexMain"];
    pd.fragmentFunction = [library newFunctionWithName:@"fragmentMain"];
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
    pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:pd error:&error];
    if (!pipeline) { NSLog(@"pipeline: %@",error); return 3; }
    MTLDepthStencilDescriptor* ds = [MTLDepthStencilDescriptor new];
    ds.depthCompareFunction = MTLCompareFunctionLess;
    ds.depthWriteEnabled = YES;
    id<MTLDepthStencilState> depthState = [device newDepthStencilStateWithDescriptor:ds];
    MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:1280 height:720 mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget;
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> color = [device newTextureWithDescriptor:td];
    td.pixelFormat = MTLPixelFormatDepth32Float;
    td.storageMode = MTLStorageModePrivate;
    id<MTLTexture> depth = [device newTextureWithDescriptor:td];
    id<MTLCommandQueue> queue = [device newCommandQueue];
    int index = 0;
    for (NSNumber* angle in spec[@"angles"]) {
        const double yaw = angle.doubleValue, s=std::sin(yaw), c=std::cos(yaw);
        const double direction[3] = {c-s,s+c,1.0};
        std::vector<Vertex> vertices;
        for (const auto& cell:cells) for (int axis=0;axis<3;axis++) {
            if (std::abs(direction[axis]) < 1e-6) continue;
            const int sign=direction[axis]>0?-1:1;
            auto neighbor=cell; neighbor[axis]+=sign;
            if (cells.count(neighbor)) continue;
            int spans[2], n=0; for(int j=0;j<3;j++) if(j!=axis) spans[n++]=j;
            Vertex corners[4]; const int uv[4][2]={{0,0},{1,0},{1,1},{0,1}};
            for(int q=0;q<4;q++) {
                double p[3]={(double)cell[0],(double)cell[1],(double)cell[2]};
                p[axis]+=sign*0.5; p[spans[0]]+=uv[q][0]-0.5; p[spans[1]]+=uv[q][1]-0.5;
                const double vx=c*p[0]+s*p[1],vy=-s*p[0]+c*p[1];
                corners[q]={{(float)p[0],(float)p[1],(float)p[2],(float)yaw},{0.5,0.5,0.5,1}};
                corners[q].color[axis]=sign<0?0:1;
            }
            for(int q:{0,1,2,0,2,3}) vertices.push_back(corners[q]);
        }
        id<MTLBuffer> buffer = [device newBufferWithBytes:vertices.data() length:vertices.size()*sizeof(Vertex) options:MTLResourceStorageModeShared];
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture=color;
        pass.colorAttachments[0].loadAction=MTLLoadActionClear;
        pass.colorAttachments[0].storeAction=MTLStoreActionStore;
        pass.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,1);
        pass.depthAttachment.texture=depth;
        pass.depthAttachment.loadAction=MTLLoadActionClear;
        pass.depthAttachment.storeAction=MTLStoreActionDontCare;
        pass.depthAttachment.clearDepth=1;
        id<MTLCommandBuffer> cmd=[queue commandBuffer];
        id<MTLRenderCommandEncoder> enc=[cmd renderCommandEncoderWithDescriptor:pass];
        [enc setRenderPipelineState:pipeline]; [enc setDepthStencilState:depthState];
        [enc setCullMode:MTLCullModeNone]; [enc setVertexBuffer:buffer offset:0 atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:vertices.size()];
        [enc endEncoding]; [cmd commit]; [cmd waitUntilCompleted];
        if(cmd.status!=MTLCommandBufferStatusCompleted) {NSLog(@"draw: %@",cmd.error);return 4;}
        std::vector<unsigned char> pixels(1280*720*4);
        [color getBytes:pixels.data() bytesPerRow:1280*4 fromRegion:MTLRegionMake2D(0,0,1280,720) mipmapLevel:0];
        std::string path=std::string(argv[2])+"/shot-"+std::to_string(index++)+".rgba";
        std::ofstream out(path,std::ios::binary);out.write((char*)pixels.data(),pixels.size());
        if(!out) return 5;
        std::cout<<path<<" triangles="<<vertices.size()/3<<std::endl;
    }
    return 0;
} }
