#!/usr/bin/env python3

import licant

toolchain = licant.gcc_toolchain("")

licant.cxx_application("sort-fuse",
	toolchain=toolchain,
	sources=["main.cpp"],
	include_paths=["/usr/local/include/fuse3"],
	libs=["fuse3", "pthread", "nos", "igris"],
	cxxstd="gnu++20",
	cxx_flags="-g"
)

licant.ex("sort-fuse")
