# Description:
#   Protocol buffers.

package(default_visibility = ["//visibility:public"])

licenses(["notice"])  # Apache 2.0

load("//cpu_instructions:proto_library.bzl", "cpu_instructions_proto_library")

# Represents CPU microarchitectures and models.

cpu_instructions_proto_library(
    name = "cpu_type_proto",
    srcs = ["cpu_type.proto"],
    cc_api_version = 2,
    js_api_version = 2,
)

# Represents instruction sets for a given cpu, as well as information gathered
# on every instruction.

cpu_instructions_proto_library(
    name = "instructions_proto",
    srcs = ["instructions.proto"],
    cc_api_version = 2,
    js_api_version = 2,
    deps = [
        ":cpu_type_proto",
        "//cpu_instructions/proto/x86:encoding_specification_proto",
    ],
)
