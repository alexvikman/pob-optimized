# Path of Building – native host

A lightweight native host (C++20, SDL3, LuaJIT) that runs the **unmodified**
[Path of Building](https://github.com/PathOfBuildingCommunity/PathOfBuilding) Lua
code on macOS (Apple Silicon) and Windows x64.

PoB itself lives in the `PathOfBuilding/` submodule and is never modified; everything
platform specific is implemented in the host.

## Building

Requirements: CMake ≥ 3.24, a C++20 compiler (Apple Clang / Xcode Command Line Tools
on macOS, MSVC on Windows) and git. All other dependencies are fetched at pinned
revisions during configure.

```bash
git clone --recurse-submodules https://github.com/alexvikman/path-of-building-mac.git
cmake -S . -B build
cmake --build build -j8
./build/host/PathOfBuilding
```

On first start the host copies PoB from the `PathOfBuilding/` submodule into its data
directory (`~/Library/Application Support/PathOfBuilding-Native/` on macOS) and runs
it from there.

### Development options

```bash
# Use a throwaway data directory instead of the real one
./build/host/PathOfBuilding --data build/pobdata

# Self test: boot to the main menu, run a subscript round trip and a Restart(),
# exit 0 on success. --eval runs a Lua file inside PoB after boot.
./build/host/PathOfBuilding --data build/pobdata --check-boot [--eval probe.lua]

# Scripted mouse/keyboard input after boot (see host/app/InputScript.h)
./build/host/PathOfBuilding --data build/pobdata --check-boot --input steps.txt

# ...and save a PNG of the window after boot/eval/input; --size sets the window size
./build/host/PathOfBuilding --data build/pobdata --check-boot --screenshot shot.png --size 1440x900

# Per-frame timings in the log
POB_HOST_STATS=1 ./build/host/PathOfBuilding --data build/pobdata
```

The log is written to `host.log` in the data directory.

## Dependencies

| Library | Version | Notes |
|---|---|---|
| SDL3 | 3.4.18 | window, input, clipboard, rendering (Metal / D3D) |
| LuaJIT | v2.1 @ `ea9eb7e` | static, built with its own makefile |
| zlib | 1.3.2 | |
| stb | `003501f` | stb_image |
| libcurl | system on macOS, 8.23.0 (Schannel) on Windows | |
| libwebp | 1.6.0 | decoder only; PoB ships WebP tree art |
| Lua-cURLv3, luautf8, LuaSocket | SimpleGraphic's pinned revisions | `lcurl`, `lua-utf8`, `socket.core` Lua modules |

## Licenses

Path of Building and SimpleGraphic are both MIT licensed (© David Gowor and the
PoB Community). Code adapted from SimpleGraphic keeps its notice; see
[LICENSE-THIRD-PARTY.md](LICENSE-THIRD-PARTY.md).
