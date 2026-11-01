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
git clone --recurse-submodules https://github.com/alexvikman/pob-optimized.git
cmake -S . -B build
cmake --build build -j8
./build/host/PathOfBuilding
```

On first start the host copies PoB from the `PathOfBuilding/` submodule into its data
directory (`~/Library/Application Support/PathOfBuilding-Native/` on macOS) and runs
it from there.

### Packaging

```bash
# macOS: dist/Path of Building.app, ad-hoc signed
packaging/macos/make-app.sh build dist
```

```powershell
# Windows (from a Visual Studio developer prompt): dist/PathOfBuilding-win64.zip,
# plus dist/PathOfBuilding-Setup-win64.exe when NSIS is installed
packaging/windows/package.ps1 -Build build -Out dist
```

The Windows setup (`packaging/windows/installer.nsi`) installs per user without
administrator rights, like the official PoB setup. It adds Start menu and desktop
shortcuts and an uninstaller under Settings › Apps.

GitHub Actions (`.github/workflows/build.yml`) builds both on every push, runs the
unit tests and the `--check-boot` self test on the packaged app, installs, runs and
uninstalls the Windows setup, and uploads the `.app` zip, the Windows setup and the
Windows zip as artifacts.

**Releases:** pushing a tag `vX.Y.Z` that matches `VERSION` in `CMakeLists.txt`
builds both platforms and creates a **draft** GitHub release with both packages and
the notes from `packaging/release-notes.md`. Review the draft on GitHub, then
publish it.

```bash
git tag v0.1.0 && git push origin v0.1.0
```

The macOS app is ad-hoc signed, not notarised. When it is downloaded from the
internet, macOS blocks the first start: right-click the app and choose **Open**, or
clear the quarantine flag:

```bash
xattr -dr com.apple.quarantine "/Applications/Path of Building.app"
```

### Development options

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

### Power use

The host only redraws when something happens. With the passive tree idle it uses
about 0.8 % CPU on macOS, against 52 % when redrawing continuously like the
original. On Windows (measured in a VM) it uses 5.3 % focused and 0 % in the
background, against 83 %. The
rates can be set in `host-settings.ini`; see [docs/PERFORMANCE.md](docs/PERFORMANCE.md).

### Updates

PoB updates itself from the official repository (`master` branch) through its own
"Update" button, as on Windows. The host installs PoB with the platform
`native-macos`/`native-win64` and normalises PoB's manifest so that no Windows
binaries are downloaded. See docs/API.md §8. To try it with an older bundle:

```bash
git clone --depth 1 --branch v2.66.0 https://github.com/PathOfBuildingCommunity/PathOfBuilding.git build/pob-old
./build/host/PathOfBuilding --bundle build/pob-old --data build/upd-data --user build/upd-user
```

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

The native host is MIT licensed; see [LICENSE](LICENSE). Path of Building and
SimpleGraphic are also MIT licensed (© David Gowor and the PoB Community). Code
adapted from SimpleGraphic keeps its notice; see
[LICENSE-THIRD-PARTY.md](LICENSE-THIRD-PARTY.md).
