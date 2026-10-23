#import <Foundation/Foundation.h>
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
