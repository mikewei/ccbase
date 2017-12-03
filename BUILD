package(default_visibility = ["//visibility:public"])

cc_library(
  name = "ccbase",
  includes = ["src"],
  copts = [
    "-g",
    "-O2",
    "-Wall",
  ],
  linkopts = [
    "-lrt",
    "-pthread",
  ],
  nocopts = "-fPIC",
  linkstatic = 1,
  srcs = glob([
    "src/ccbase/*.cc",
    "src/ccbase/*.h",
  ]),
  deps = [],
)

cc_test(
  name = "test",
  copts = [
    "-g",
    "-O2",
    "-Wall",
  ],
  nocopts = "-fPIC",
  linkstatic = 1,
  srcs = glob(["test/*_test.cc"]),
  deps = [
    ":ccbase",
    "//gtestx",
  ],
)

cc_library(
  name = "ccbase_diag",
  includes = ["src"],
  copts = [
    "-g",
    "-O2",
    "-Wall",
    "-fsanitize=address",
  ],
  linkopts = [
    "-lrt",
    "-pthread",
  ],
  nocopts = "-fPIC",
  linkstatic = 1,
  srcs = glob([
    "src/ccbase/*.cc",
    "src/ccbase/*.h",
  ]),
  deps = [],
)

cc_test(
  name = "test_diag",
  copts = [
    "-g",
    "-O2",
    "-Wall",
    "-fsanitize=address",
  ],
  linkopts = [
    "-g",
    "-fsanitize=address",
    "-static-libasan",
  ],
  nocopts = "-fPIC",
  linkstatic = 1,
  srcs = glob(["test/*_test.cc"]),
  deps = [
    ":ccbase_diag",
    "//gtestx",
  ],
)

