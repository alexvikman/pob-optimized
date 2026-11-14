# Developing the native port

The port is a lightweight host (C++20, SDL3, LuaJIT) that runs the **unmodified**
Path of Building Lua code. PoB lives in the `PathOfBuilding/` submodule and is never
modified; everything platform specific is implemented in the host. The host provides
the Lua API that PoB expects from SimpleGraphic; [API.md](API.md) is that contract,
global by global.

## Building

Requirements: CMake ≥ 3.24, a C++20 compiler (Apple Clang / Xcode Command Line Tools
on macOS, MSVC on Windows) and git. All other dependencies are fetched at pinned
revisions during configure.

```bash
git clone --recurse-submodules https://github.com/alexvikman/pob-optimized.git
cmake -S . -B build
cmake --build build -j8
./build/host/PathOfBuilding
```

On first start the host copies PoB from the `PathOfBuilding/` submodule into its data
directory (`~/Library/Application Support/PathOfBuilding-Native/` on macOS) and runs
it from there.

Only development builds fall back to the submodule like this. A Release build
(`-DCMAKE_BUILD_TYPE=Release`, what CI ships) looks for PoB next to the executable and
nowhere else, so that a path from the build machine is never trusted on a user's
machine. To run one straight from the build directory, pass `--bundle PathOfBuilding`.

## Development options

```bash
# Use throwaway data and user directories instead of the real ones
./build/host/PathOfBuilding --data build/pobdata --user build/pobuser

# Self test: boot to the main menu, run a subscript round trip and a Restart(),
# exit 0 on success. --eval runs a Lua file inside PoB after boot.
./build/host/PathOfBuilding --data build/pobdata --check-boot [--eval probe.lua]

# Scripted mouse/keyboard input after boot (see host/app/InputScript.h)
./build/host/PathOfBuilding --data build/pobdata --check-boot --input steps.txt

# ...and save a PNG of the window after boot/eval/input; --size sets the window size
./build/host/PathOfBuilding --data build/pobdata --check-boot --screenshot shot.png --size 1440x900

# Frame rate and timings in the log every 5 s
POB_HOST_STATS=1 ./build/host/PathOfBuilding --data build/pobdata

# Unit tests
ctest --test-dir build --output-on-failure
```

The log is written to `host.log` in the data directory.

## Power use

The host only redraws when something happens, paces slow animations by how far they
move, and skips the GPU for frames identical to the one on screen. How the loop works,
its settings in `host-settings.ini` and the measurements are in
[PERFORMANCE.md](PERFORMANCE.md).

## PoB content updates

PoB updates itself from the official repository (`master` branch) through its own
"Update" button. The host installs PoB with the platform `native-macos`/`native-win64`
and normalises PoB's manifest so that no Windows binaries are downloaded; see
[API.md](API.md) §8. To try an update from an older bundle:

```bash
git clone --depth 1 --branch v2.66.0 https://github.com/PathOfBuildingCommunity/PathOfBuilding.git build/pob-old
./build/host/PathOfBuilding --bundle build/pob-old --data build/upd-data --user build/upd-user
```

The host's own update check (a daily look at this repository's latest release) is in
`host/app/AppUpdate.cpp`.

## Layout

| Path | What |
|---|---|
| `host/main.cpp` | command line |
| `host/app/` | window, main loop, frame scheduler, input, self test, update notice |
| `host/core/` | paths, install and PoB manifest handling, log |
| `host/lua/` | the Lua API PoB calls (core, files, rendering), modules, subscripts |
| `host/render/` | draw list, renderer, fonts and text, textures |
| `host/tests/` | unit tests |
| `packaging/` | macOS app bundle, Windows zip and setup, icons, release notes |
| `tools/` | `ci-wait.sh`, `update-pob.sh` |
| `PathOfBuilding/` | the PoB submodule, never modified |

## Dependencies

| Library | Version | Notes |
|---|---|---|
| SDL3 | 3.4.18 | window, input, clipboard, rendering (Metal / D3D) |
| LuaJIT | v2.1 @ `ea9eb7e` | static, built with its own makefile |
| zlib | 1.3.2 | |
| stb | `003501f` | stb_image |
| libcurl | system on macOS, 8.23.0 (Schannel) on Windows | linked statically on Windows; no DLLs ship |
| libwebp | 1.6.0 | decoder only; PoB ships WebP tree art |
| Lua-cURLv3, luautf8, LuaSocket | SimpleGraphic's pinned revisions | `lcurl`, `lua-utf8`, `socket.core` Lua modules |

All are pinned in `cmake/Dependencies.cmake`, `cmake/LuaJIT.cmake` and
`cmake/LuaModules.cmake`, each to a full commit hash: a tag name can be moved to other
code upstream, a hash cannot. To move to a new release, change the hash and the
release named in the comment beside it (`git ls-remote <repo> 'refs/tags/<tag>*'`
prints the hash; for an annotated tag it is the `^{}` line).

## Packages and releases

Packaging, CI and how a release is made: [RELEASING.md](RELEASING.md).
