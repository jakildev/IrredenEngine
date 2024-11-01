load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")

cmake(
    name = "easy_profiler",
    lib_source = "@easy_profiler//:all_srcs",
    # out_static_libs = ["libeasy_profiler.a"],
    out_shared_libs = ["libeasy_profiler.dylib"],
    visibility = ["//visibility:private"],
    cache_entries = {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_SYSTEM_NAME": "Darwin",  # Correct system name for macOS
        "CMAKE_SYSTEM_PROCESSOR": "arm64",
        "CMAKE_OSX_ARCHITECTURES": "arm64",
    },
    # build_script= "//:custom_build_cmake.sh"
)

cc_library(
    name = "ir_profile",
    srcs = 
        glob(["src/**/*.cpp"])
    ,
    hdrs = glob(
        ["include/**/*.hpp", "include/**/*.tpp", ]
    ),
    deps = [
        '@spdlog',
        ':easy_profiler'
    ],
    strip_include_prefix = "include",
    include_prefix = "",
    visibility = ["//visibility:public"],
)