load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")

cmake(
    name = "easy_profiler",
    lib_source = "@easy_profiler//:all_srcs",
    # out_static_libs = ["libeasy_profiler.a"],
    out_shared_libs = ["libeasy_profiler.dylib"],
    visibility = ["//visibility:private"],
    cache_entries = {
        "CMAKE_BUILD_TYPE": "Release"
    },
)

cc_library(
    name = "ir_profile",
    srcs = 
        glob(["src/**/*.cpp"])
    ,
    hdrs = glob(
        ["include/**/*.hpp", "include/**/*.tpp"]
    ),
    deps = [
        '@spdlog',
        ':easy_profiler'
    ],
    strip_include_prefix = "include",
    include_prefix = "",
    visibility = ["//visibility:public"],
)