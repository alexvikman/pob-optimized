# Power use and frame pacing

Path of Building draws its whole UI in immediate mode: every `OnFrame` call
redraws everything. The original Windows build calls it continuously (vsync),
which keeps a CPU core busy and the fans spinning even when nothing happens on
screen. The PoB Lua code is unchanged, so the host controls **when** `OnFrame`
runs and **whether** the result is sent to the GPU.

## How the loop works

Implemented in `host/app/FrameScheduler.cpp` and `host/app/App.cpp`:

| Situation | Frame rate |
|---|---|
| Mouse/keyboard input, and for `activeMs` (500 ms) afterwards | up to `maxFps` (60) |
| PoB has background work (yielded coroutines) or is booting | up to `maxFps` |
| Focused, content changes on consecutive frames without input (an animation) | paced by how fast it moves: one frame per `animationStepPx` (1 px), between `maxFps` and `idleFps` |
| Focused, idle | `idleFps` (4), enough for the blinking caret |
| Focused, and the last 8 frames were identical | `slowIdleFps` (0.5) |
| Unfocused | `backgroundFps` (0 = only on events), animations included |
| Hidden, minimised or fully covered | only on events |

Some events always get the next frame, still capped at `maxFps`:

- a subscript sending a call or finishing (downloads, imports, update check),
- an asynchronous image load completing,
- expose, resize and focus changes.

The loop sleeps in `SDL_WaitEventTimeout` until the next frame is due or an event
arrives. Nothing spins.

**Identical frames skip the GPU.** After `OnFrame` the draw list is hashed (FNV-1a
over every quad). If the hash equals the frame already on screen, and no image
finished loading in the meantime, nothing is rendered or presented. The Lua work
still happens, but at idle rates it is small.

**Slow animations are paced by their speed.** The passive tree rotates the
radius rings of allocated timeless jewels (and other jewels with a radius) while
"Show Animations" is on. They turn about 3 px per second, so at full rate every
frame differs from the last by a twentieth of a pixel, and the whole tree is
recomputed and rendered 60 times a second for it. When a frame differs from the
previous one only in quad positions, the draw list reports the largest vertex
movement (`DrawList::MotionSincePrevious`). The scheduler spaces the next frames
so that each one moves about `animationStepPx`. Anything else that changes
(quads added or removed, colours, textures) counts as fast and gets `maxFps`, as
do toasts and scrolling, which move several pixels per frame.

While the mouse moves, frames run at full rate for responsiveness, and the rings
would make every one of them differ from the last. The host therefore also keeps
a copy of the frame on screen and skips the GPU while the new frame differs from
it only by positions that moved less than half of `animationStepPx`. Hover
highlights and tooltips are real changes and are drawn at once.

**Live resize.** On macOS, dragging the window edge runs a modal loop. The host
redraws from an SDL event watch on the live-resize expose events, so the content
follows the window.

### Settings

`host-settings.ini` in the data directory
(`~/Library/Application Support/PathOfBuilding-Native/` on macOS) is created with
the defaults and comments on first start:

```ini
mode = adaptive          # or "continuous" to behave like the original
maxFps = 60
activeMs = 500
animationStepPx = 1
idleFps = 4
slowIdleFps = 0.5
slowIdleAfter = 8
backgroundFps = 0
skipIdenticalFrames = true
vsync = true
```

`POB_HOST_STATS=1` logs frame rate, rendered frames and timings every 5 seconds to
`host.log`.

## Measurements

The tests ran on macOS 27.2 on Apple Silicon (arm64), with a 1280×800-point window
on a Retina display (2560×1600 pixels). The window was focused and nobody touched
it. It showed the passive tree of a new build, which is PoB's most expensive view.

Each sample covered 20 s after a 10 s warm-up, taken with
`top -l 21 -s 1 -stats pid,cpu,power`. POWER is macOS's energy impact, the same
figure Activity Monitor shows. Both modes are the same binary; only `mode` in
`host-settings.ini` changed.

| Mode | CPU (avg) | Energy impact (avg) | `OnFrame` calls | GPU frames |
|---|---|---|---|---|
| `continuous` (behaves like the original) | **51.9 %** | **51.9** | ~54/s | every frame |
| `adaptive` (default) | **0.8 %** | **0.8** | 0.5/s | 0 |

What it costs to use the app:

| Scenario (adaptive) | `OnFrame` calls | GPU frames |
|---|---|---|
| Moving the mouse over the tree | up to 60/s while moving (38/s on average over 7 s) | only when the picture changes (16 of 265) |
| Search field focused, caret blinking | 4/s | 2/s (one per blink) |
| Idle, nothing focused | 0.5/s | 0 |
| Window in the background | 0 | 0 |

Per frame in the tree view, PoB's `OnFrame` takes 4–15 ms of Lua, and up to 40 ms
while a build is loading or the tree search is active. Rendering takes 2–5 ms for
about 10,000 quads.

### Windows

The tests ran in a Windows 11 24H2 VM (VMPal on the same Mac, 4 vCPUs, GPU
acceleration on). The Mac is Apple Silicon, so the VM runs **Windows on ARM**, and
the x64 build runs through Microsoft's Prism emulation. Absolute numbers are
therefore not those of an x64 PC. The two modes still compare fairly, since they
run the same binary in the same VM. CPU is `TotalProcessorTime` over 20 s after a
15 s warm-up, with the passive tree of a new build and the window left alone.

| Mode | Window | CPU (one core = 100 %) | `OnFrame` calls | GPU frames |
|---|---|---|---|---|
| `continuous` (behaves like the original) | in front | **83.1 %** | 55/s | every frame |
| `adaptive` | focused | **5.3 %** | 0.5/s | 0 |
| `adaptive` | in the background | **0.0 %** | 0 | 0 |

Under emulation a single `OnFrame` costs 35–45 ms (4–15 ms natively on the Mac),
which is why the adaptive figure for a focused window is higher than on macOS.

The test also turned up a packaging bug: the exe needed the Visual C++
Redistributable (`VCRUNTIME140_1.dll`), which CI runners have but a clean Windows
does not. Everything now links the runtime statically, and CI fails if the exe
depends on `vcruntime`/`msvcp` DLLs.

### A build with a timeless jewel

Same Mac and window as above. The window was focused and left alone, with the tree
of an imported build that has a Lethal Pride jewel allocated (the rings rotate).

| Version | `OnFrame` calls | GPU frames | Lua + render per second |
|---|---|---|---|
| Before speed-based pacing | 55.7/s | every frame | ~590 ms (59 % of a core) |
| `animationStepPx = 1` (default) | 5.4/s | every frame | ~57 ms (6 % of a core) |

On a display without Retina scaling the same rotation is half as many pixels, so
the rate drops to around 3–4/s; it never goes below `idleFps`.

Moving the mouse across the same tree for 10 s (`OnFrame` runs at ~50/s either
way):

| Version | GPU frames per 5 s |
|---|---|
| Every changed frame rendered (`animationStepPx = 0`) | ~260 (all) |
| Sub-pixel ring movement skipped (default) | ~48 |

### Not measured yet

- **GPU power on its own.** `sudo powermetrics --samplers gpu_power` needs root.
  Energy impact covers the whole process, and in adaptive idle the GPU renders
  nothing.
- **Live resize.** It needs a person dragging the window edge.

To reproduce:

```bash
# behaves like the original
sed -i '' 's/^mode = .*/mode = continuous/' ~/Library/Application\ Support/PathOfBuilding-Native/host-settings.ini
# open the tree, leave the window alone, then:
top -l 21 -s 1 -pid $(pgrep -x PathOfBuilding) -stats pid,cpu,power
```
