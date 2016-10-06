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
