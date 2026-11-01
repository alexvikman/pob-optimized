# Host API contract

This document is the contract between the unmodified Path of Building Lua code and
our native host. It lists every global the host must provide, how each one behaves,
and the environment PoB assumes it runs in.

**Sources** (behaviour verified against these exact revisions):

| Source | Revision | Used for |
|---|---|---|
| PathOfBuilding | `v2.67.2` (`b32759a`), also checked against `dev` (`3dbca44`) | what PoB calls and expects |
| PathOfBuilding-SimpleGraphic | `master` (`8694a07`, 2026‑09‑14) | reference behaviour |
| `src/HeadlessWrapper.lua` | PoB `v2.67.2` | the stub list PoB itself keeps |

SimpleGraphic (SG) is MIT licensed (© 2016 David Gowor). We use it as a behavioural
reference. Any code adapted from it must keep that copyright notice.

File references such as `ui_api.cpp:1079` point into SimpleGraphic, and `Launch.lua:68`
points into `PathOfBuilding/src`.

**Usage counts** come from a bytecode scan (`luajit -bl`) of every `.lua` file in `src/`
and `runtime/lua/`, excluding `HeadlessWrapper.lua` and `src/Export/`. Each count is the
number of `GGET` instructions, i.e. reads of the global. Calls made from inside subscript
source strings do not show up in the scan; those were found with grep and are listed in
§7.

---

## 1. Environment and lifecycle

### 1.1 Script start (`ui_main.cpp:220`, `ui_main.cpp:281`)

1. The script is `Launch.lua` in the **script directory** (`GetScriptPath()`).
   `scriptPath` and `scriptWorkDir` both start as the parent directory of the script.
2. A fresh `lua_State`. `luaL_openlibs` is called, and `package.path` gets
   `;lua/?.lua` appended. That entry resolves against the current working directory,
   which is the runtime directory when `require` runs from the main script.
3. The registry holds a `traceback` function, used by `PCall`/`PLoadModule`.
4. The script file is loaded with `luaL_loadfile`. Several PoB files begin with a
   `#@` line (`Launch.lua`, `UpdateCheck.lua`, `UpdateApply.lua`, `LaunchInstall.lua`).
   The LuaJIT that SG pins (2022) skipped any first line starting with `#`. **Current
   LuaJIT only skips `#!` and parses other `#` lines as line-number directives**, so
   loading `Launch.lua` fails with "unexpected symbol near '#'". The host therefore
   loads every file itself (`LoadLuaFile`): it blanks a leading `#` line, keeping the
   line numbers, and loads the result with chunk name `@<relative path>`.
5. The script runs with the `argv` strings as arguments, and a global `arg` table is
   set (`arg[0]` = script).
6. If the script did not exit or restart: call `OnInit`.
7. If there is no `OnFrame` callback, the script exits.
8. The frame loop runs (§1.3).

The host also sets `os.exit` to `Exit` (`ui_api.cpp:2293`).

### 1.2 Working directory rules

PoB opens many files by **relative** path (`io.open("first.run")`,
`xml.LoadXMLFile("manifest.xml")`, `io.open("UpdateCheck.lua")`,
`io.open("installed.cfg")`, `io.open("LaunchServer.lua")`). SG makes these work by
setting the process cwd to `scriptWorkDir` around every Lua entry and restoring it
afterwards (`ui_main.cpp:149`, `PCall`).

- During any callback the process cwd **must** equal `scriptWorkDir`, which is
  initially the script directory.
- `LoadModule`/`PLoadModule` temporarily switch cwd to `scriptPath` while loading the
  file, then switch back to `scriptWorkDir` (`ui_api.cpp:1864`).
- `SetWorkDir(path)` changes `scriptWorkDir` (§3.6).

Our host is single-threaded on the Lua side, so we can simply `chdir` to the script
directory once and keep it there. Subscript threads must not depend on cwd changing,
because cwd is process-wide.

### 1.3 Frame loop (`ui_main.cpp:371`)

Each SG frame does the following:

1. `renderer->BeginFrame()`: resets to layer (0,0), sets the full viewport and white
   draw colour, and samples the cursor position.
2. `renderEnable = true`. Draw calls made outside this window raise an error
   ("… called outside of OnFrame").
3. For each live subscript, `SubScriptFrame()` delivers queued `OnSubCall`s, answers a
   pending function call, and fires `OnSubFinished`/`OnSubError` (§5).
4. Call `OnFrame`.
5. `renderEnable = false`. Draw the console on layer 10000, then `EndFrame()`: sort
   layers, hash the command lists, skip the GPU work if the hash is unchanged
   ("frame elision"), and present.
6. If `Restart()` was called: `OnExit` → destroy the state → reinitialise from step 1.1.

SG throttling is a 100 ms sleep when the window is inactive and no coroutine or
subscript is active. Otherwise it spins at vsync. **Our host replaces this with the
event-driven loop from milestone M6.** The parts that matter for correctness:

- **Active coroutines.** After every protected call SG calls `coroutine._list()`. PoB
  defines it in `Common.lua:54` as a weak table of coroutines created through its
  wrapped `coroutine.create`. If any coroutine has status `LUA_YIELD`, PoB has
  background work in progress (build list loading, timeless jewel data, and so on) and
  needs frames to keep coming. **Our loop must keep running frames while that is true.**
- **Live subscripts** also need frames, so that their results get delivered.
- SG always runs 10 frames after boot.

### 1.4 Errors

Every callback goes through `PCall` with the traceback handler. An error that escapes
it calls `DoError` ("--- SCRIPT ERROR ---"), which shows the message and stops the
script. PoB catches almost everything itself and shows its own error popup
(`launch:ShowErrMsg`), so a host-level error means something is seriously broken. The
host shows a native error dialog and offers the log.

### 1.5 Exit and restart

| Function | SG behaviour |
|---|---|
| `Exit([msg])` | Sets `didExit`. With a message, the window is hidden and the message is shown in the console window until it is closed. The loop ends after the current call returns. |
| `Restart()` | Sets `restartFlag`. **At the end of the current frame**: `OnExit`, then the Lua state is closed, shaders are purged, and the script reinitialises from scratch (new state, `Launch.lua` again). The window and renderer survive. |
| Window close | Calls `CanExit()` first. A false return cancels the close. |

---

## 2. Callbacks

Callbacks are looked up first in the table filled by `SetCallback(name, fn)`, then as
a method on the object given to `SetMainObject(obj)` (`ui_main.cpp:120`
`PushCallback`). Method calls pass `obj` as `self`. PoB uses `SetMainObject(launch)`
and never calls `SetCallback`.

| Callback | Arguments | Return | When |
|---|---|---|---|
| `OnInit()` | – | – | once, after the main chunk runs |
| `OnFrame()` | – | – | every frame (§1.3) |
| `OnKeyDown(key, doubleClick)` | key name (§4.1), boolean | – | key or mouse button pressed, or key auto-repeat. `doubleClick` is true for a mouse double click (≤ 500 ms, ≤ 5 px). |
| `OnKeyUp(key)` | key name | – | release. The wheel sends Down and then Up immediately. |
| `OnChar(ch)` | 1-char string | – | text input (§4.2) |
| `CanExit()` | – | boolean | the user tries to close the window |
| `OnExit()` | – | – | before shutdown or restart |
| `OnSubCall(name, ...)` | name of a sub/func, then its args | values (func calls only) | §5 |
| `OnSubFinished(id, ...)` | subscript id, return values | – | §5 |
| `OnSubError(id, msg)` | subscript id, error string | – | §5 |

`SetCallback(name[, fn])`, `GetCallback(name)` and `SetMainObject([obj])` must exist.
PoB calls only `SetMainObject` (1 use).

---

## 3. Functions

Unless noted otherwise, argument validation raises a Lua error with SG's message
(`"Fn() argument N: expected T, got U"`). We match the messages where it costs nothing.

### 3.1 Rendering setup

| Function | Uses | Semantics | Ref |
|---|---|---|---|
| `RenderInit(...flags)` | 1 | Creates the renderer. `"DPI_AWARE"` turns on the DPI-aware virtual screen (§3.9). Only the first call has an effect. PoB calls it from `OnInit`, **after** the update check and the first-run logic (`Launch.lua:68`), so `NewImageHandle():Load` and similar must not run before it. | `ui_api.cpp:705` |
| `GetScreenSize()` | (Common.lua) | `width, height` (integers) of the virtual screen. With `DPI_AWARE` this is the **framebuffer size in physical pixels**. PoB divides it by `GetScreenScale()` itself (`GetVirtualScreenSize`, `Common.lua:1056`). | `ui_api.cpp:725`, `r_main.cpp:1638` |
| `GetScreenScale()` | (Common.lua) | DPI scale factor. It is the override percentage / 100 if an override is set, otherwise the window content scale, and 1.0 without `DPI_AWARE`. | `r_main.cpp:1648` |
| `SetDPIScaleOverridePercent(p)` | 5 | Sets the override; 0 means "use the system scale". PoB stores it in Settings. | `ui_api.cpp:1186` |
| `GetDPIScaleOverridePercent()` | 2 | integer | `ui_api.cpp:1198` |
| `SetClearColor(r, g, b[, a])` | 0 | Background clear colour (0–1 floats). | `ui_api.cpp:740` |
| `GetAsyncCount()` | 0 | Number of textures still loading asynchronously. | `ui_api.cpp:1230` |

### 3.2 Draw state

All of these are valid only inside `OnFrame` (an error otherwise).

| Function | Uses | Semantics | Ref |
|---|---|---|---|
| `SetDrawLayer(layer[, subLayer])` / `SetDrawLayer(nil, subLayer)` | 93 | Selects the command list `(layer, subLayer)`, creating it on demand. Leaving out `subLayer` means **0**. `nil` as `layer` keeps the current layer and changes only the sublayer. Selecting a layer re-emits the current viewport into it. | `ui_api.cpp:762`, `r_main.cpp:1456` |
| `GetDrawLayer()` | 0 | Returns the current **sublayer** (sic). | `r_main.cpp:1486` |
| `SetViewport([x, y, w, h])` | 49 | Logical coordinates × scale: `x, y` are rounded (`lround`) and `w, h` rounded up (`ceil`). With no arguments the viewport is the full screen. The viewport **translates and clips**: drawing coordinates are relative to `(x, y)`, and anything outside the rectangle is scissored away. It applies to the current layer from this point on. | `ui_api.cpp:793`, `r_main.cpp:1491` |
| `SetDrawColor(r, g, b[, a])` / `SetDrawColor("^7")` | 362 | Sets the tint for later images and text. Alpha defaults to 1. In the string form any colour escape (§4.4) works and alpha becomes 1. | `ui_api.cpp:817` |
| `GetDrawColor()` | 1 | `r, g, b, a` of the colour last set by `SetDrawColor` **or left over by the last `DrawString`**, since escape codes inside text change it. | `ui_api.cpp:867` |

At the start of each frame the state is layer (0, 0), the full viewport and white.

**Layer ordering.** Layers are drawn in ascending `(layer, subLayer)` order. Within a
layer, commands are drawn in submission order. PoB uses layers 0–1000 heavily for
tooltips and popups (`SetDrawLayer(nil, 100)` and similar). The console uses 10000.

### 3.3 Drawing

| Function | Uses | Semantics | Ref |
|---|---|---|---|
| `DrawImage(img\|nil, left, top, w, h[, tcLeft, tcTop, tcRight, tcBottom][, stackIdx])` | 236 | Draws an axis-aligned textured quad. Allowed argument counts are 5, 6, 9 and 10; any other count is an error. `nil` draws a solid rectangle in the draw colour. Positions are multiplied by the DPI scale and texture coordinates are 0–1 (default 0,0–1,1). `stackIdx` (1-based) selects a texture-array layer. | `ui_api.cpp:880` |
| `DrawImageQuad(img\|nil, x1,y1, x2,y2, x3,y3, x4,y4[, s1,t1, … s4,t4][, stackIdx])` | 17 | Draws a free quad with corners in order. Allowed argument counts are 9, 10, 17 and 18. The default UVs are (0,0) (1,0) (1,1) (0,1). The passive tree uses it for rotated connector art. | `ui_api.cpp:979` |
| `DrawString(left, top, align, height, font, text)` | 99 | §4.3 | `ui_api.cpp:1079` |
| `DrawStringWidth(height, font, text)` | 81 | Width in logical units (a float). It is the widest line; colour escapes don't count. | `ui_api.cpp:1126` |
| `DrawStringCursorIndex(height, font, text, x, y)` | 3 | **1-based byte index** into `text` for the caret position nearest to `(x, y)`, relative to the text origin. Used by `EditControl` for mouse caret placement and must match the width measurement exactly. | `ui_api.cpp:1153` |
| `StripEscapes(text)` | 10 | Removes `^N` and `^xRRGGBB` escapes. | `ui_api.cpp:1206` |

Pixel snapping: `DrawString` rounds the start x and floors y, and glyph advances are
rounded up per glyph (§4.3). Images are not snapped.

### 3.4 Image handles

`NewImageHandle()` (51 uses) returns a userdata with the methods below. The metatable
lives in the registry as `uiimghandlemeta`. `__gc` releases the texture.

| Method | Semantics | Ref |
|---|---|---|
| `:Load(path, ...flags)` | Path relative to `scriptWorkDir`. Flags: `"ASYNC"`, `"CLAMP"` (clamp-to-edge instead of repeat), `"MIPMAP"`, `"NEAREST"`. An unknown flag is an error. Non-string extra arguments are ignored (PoB passes `nil`). Textures are shared by `(path case-insensitive, flags)`. A file that fails to load still yields a valid handle. It draws SG's 8×8 grey placeholder pattern (`r_texture.cpp:605`) and reports size 0×0. | `ui_api.cpp:396`, `r_main.cpp:1366` |
| `:Unload()` | Releases the texture; `IsValid()` becomes false. | |
| `:IsValid()` | True after a `Load` until `Unload`. | |
| `:IsLoading()` | True while an async load is pending (width still 0). | `ui_api.cpp:455` |
| `:SetLoadingPriority(n)` | Priority for the async queue (higher loads first). | |
| `:ImageSize()` | `width, height`. **SG blocks until the size is known**, even for `ASYNC` loads, by reading the header. We do the same by reading the header synchronously (e.g. `stbi_info`) and decoding the pixels later. | `r_main.cpp:1419` |
| `:LoadArtRectangle(art, x1, y1, x2, y2, ...flags)` | Slices a sub-image from an art handle. **PoB does not use this** (neither v2.67.2 nor dev). | `ui_api.cpp:518` |
| `:LoadArtArcBand(art, xC, yC, rMin, rMax, ...flags)` | Masks an annulus quadrant from an art handle. Not used by PoB. | `ui_api.cpp:581` |

**Formats PoB ships:** PNG (711 files), JPEG (139), **WebP (32, e.g. `TreeData/3_28/*.webp`)**,
and TGA for the font atlases. The host therefore needs a WebP decoder (libwebp)
alongside stb_image.

**Async.** PoB loads tree art with `"ASYNC"` (`PassiveTree.lua:240`). While a texture is
pending, draws that reference it produce nothing, and the frame must not be treated as
identical to the previous one ("elision inhibited", `r_main.cpp:517`). When the upload
finishes, the main loop must wake and draw a frame.

### 3.5 Other handles (not used by PoB; low priority)

- `NewArtHandle(path)` → `:Size()`. CPU-side image used for the `LoadArt*` slicing above.
- `Texture` (sol2 usertype: `Allocate`, `Load`, `Save`, `Info`, `IsValid`,
  `StackTextures`) is tooling for array textures. Not referenced by PoB.

These are left for the unknown-global safety net (§9) unless PoB starts using them.

### 3.6 Files and paths

| Function | Uses | Semantics | Ref |
|---|---|---|---|
| `GetScriptPath()` | 6 | `path[, pathACP[, err]]`, where `path` is the UTF-8 script directory with **forward slashes and no trailing slash**. The second value is the same path in the ANSI code page (Windows); we return the UTF-8 path twice. | `ui_api.cpp:1656` |
| `GetRuntimePath()` | 3 | Same format; the directory of the runtime (exe, DLLs, `lua/`, `SimpleGraphic/Fonts`). | `ui_api.cpp:1673` |
| `GetUserPath()` | 1 | Same format. On Windows it is the **Documents** folder; PoB appends `/Path of Building/` itself (`Main.lua:95`). It can return `nil, nil, reason`. | `ui_api.cpp:1690`, `sys_main.cpp:606` |
| `GetWorkDir()` | 0 | `scriptWorkDir`. | |
| `SetWorkDir(path)` | 0 | Changes `scriptWorkDir`. | `ui_api.cpp:1767` |
| `MakeDir(path)` | 9 | Creates **one** directory level (`create_directory`). Returns `true`, or `nil, strerror`. An existing directory counts as failure, and PoB ignores that. | `ui_api.cpp:1719` |
| `RemoveDir(path[, recursive])` | 4 | `true`, or `nil, err`. Fails if the path is not a directory. | `ui_api.cpp:1739` |
| `NewFileSearch(spec[, dirsOnly])` | 14 | `spec` is `dir/pattern`, where the pattern may contain `*` and `?`. It returns a handle positioned on the first match, or `nil` if nothing matches. With `dirsOnly` it matches directories only, otherwise files only. `.` and `..` are skipped. | `ui_api.cpp:1288`, `sys_main.cpp:207` |
| `handle:NextFile()` | 12 | `true`, or `nil` when exhausted (after that the handle is invalid). | |
| `handle:GetFileName()` | 13 | File name only (no directory), UTF-8. | |
| `handle:GetFileSize()` | – | integer bytes | |
| `handle:GetFileModifiedTime()` | 6 | A number. SG returns raw `file_time_type` ticks, so the unit depends on the platform. PoB only compares values, for sorting and freshness checks. **We return seconds since the Unix epoch (float).** | |
| `GetCloudProvider(path)` | 1 | `provider, version, status` if the path is managed by a cloud sync provider (Windows Cloud Files API), otherwise no values. Used for the OneDrive error popup (`Main.lua:1724`). macOS: return nothing. | `ui_api.cpp:1448` |

Patterns PoB uses: `dir/*`, `dir/*.xml`, `Scripts/*.lua`, `x.bin`, `x.zip`, `x.zip.part*`.

### 3.7 Modules and protected calls

| Function | Uses | Semantics | Ref |
|---|---|---|---|
| `LoadModule(name, ...)` | 124 | Appends `.lua` when there is no extension. The file is loaded relative to `scriptPath` and called with `...`, returning everything. A load error is raised as `"LoadModule() error loading '<f>' (<code>):\n<msg>"`. | `ui_api.cpp:1852` |
| `PLoadModule(name, ...)` | 1 | Like `LoadModule`, but protected: returns `nil, ...results` on success or `errMsg` on failure (with a traceback). A load error returns the message. | `ui_api.cpp:1875` |
| `PCall(fn, ...)` | 28 | Returns `nil, ...results` on success and `errMsg` (with a traceback) on failure. | `ui_api.cpp:1906` |

### 3.8 Input, clipboard, system

| Function | Uses | Semantics | Ref |
|---|---|---|---|
| `GetCursorPos()` | 39 | `x, y` integers in logical units relative to the window client area: physical pixels / scale, rounded. PoB temporarily overrides it in `CompareTab.lua:3380`, so it must be a plain, replaceable global. | `ui_api.cpp:1481` |
| `SetCursorPos(x, y)` | 0 | Warps the cursor. | |
| `ShowCursor(show)` | 0 | No-op in SG. | |
| `IsKeyDown(name)` | 74 | Current held state of the key (§4.1). An unknown name is an error. PoB uses `CTRL` (50), `SHIFT` (28), `ALT` (5), `LEFTBUTTON` (5), `UP`/`DOWN`/`LEFT`/`RIGHT`. | `ui_api.cpp:1514` |
| `Copy(text)` | 16 | Sets the clipboard (UTF-8). | |
| `Paste()` | 2 | Clipboard text, or **no value** if empty. | |
| `OpenURL(url)` | 9 | Opens in the default browser. Returns an error string on failure, otherwise nothing. | `ui_api.cpp:2055` |
| `SpawnProcess(cmd[, args])` | 2 | Launches a process detached (Windows `ShellExecuteEx`, `.exe` appended). PoB uses it **only for the updater** (`Launch.lua:329`, `UpdateApply.lua:45`), see §8.4. | `ui_api.cpp:2043` |
| `SetWindowTitle(title)` | 3 | | |
| `SetForeground()` | 1 | Raises and focuses the window (after the OAuth login in `PoEAPI.lua`). | `ui_api.cpp:2107` |
| `GetTime()` | 48 | **Integer milliseconds since host start** (wall clock based in SG; we use a monotonic clock). | `sys_main.cpp:363` |
| `Deflate(data[, gzip])` | 3 | zlib (or gzip) at level 9. Inputs over 128 MiB return `nil, msg`, as do errors. Used for build codes. | `ui_api.cpp:1553` |
| `Inflate(data)` | 7 | Auto-detects zlib or gzip headers. Returns `nil, msg` on error. | `ui_api.cpp:1597` |
| `Restart()` | 3 | §1.5 | |
| `Exit([msg])` | 5 | §1.5 | |
| `TakeScreenshot()` | 1 | Ctrl+PrintScreen saves a screenshot. Low priority. | |
| `SetProfiling(on)` | 0 | No-op for us. | |

### 3.9 DPI / virtual screen

With `RenderInit("DPI_AWARE")`, which PoB always uses:

- `scale = override% / 100` if an override is set, else the window's content scale
  (2.0 on Retina).
- `GetScreenSize()` returns the framebuffer size in **physical pixels**, and PoB
  computes the logical size as `floor(physical / scale)`.
- Every coordinate PoB passes in (`DrawImage`, `DrawString`, `SetViewport`, …) is in
  **logical** units, and the host multiplies by `scale`. `GetCursorPos` divides by it.
- Text height: `h = lround(height * scale)`, then `h = 1` if `h ≤ 1`, else rounded **up
  to an even number** (`(h + 1) & ~1`). Widths are measured at that physical height
  and divided by `scale` (`ui_api.cpp:1098`, `1138`).

On macOS the content scale comes from `SDL_GetWindowDisplayScale`
(pixel density × display scale).

### 3.10 Console functions

SG has an in-window console (toggled with a key). We only need a log.

| Function | Uses | Our behaviour |
|---|---|---|
| `ConPrintf(fmt, ...)` | 135 | `string.format` (errors propagate), then log line + newline. |
| `ConPrintTable(t[, noRecurse])` | 2 | Dumps the table recursively to the log, cycle-safe. |
| `ConExecute(cmd)` | 2 | PoB sends `set vid_mode 8` and `set vid_resizable 3` (`Launch.lua:12`), SG console variables for the default window size and resizability. We log and ignore them, apart from honouring "resizable". |
| `ConClear()` | 2 | No-op. |
| `print(...)` | 14 | Replaced by SG to go to the console: values are converted with `tostring` and separated by single spaces (not tabs). We log the same way. |

---

## 4. Input and text

### 4.1 Key names (`ui_main.cpp:16`)

The key argument of `OnKeyDown`, `OnKeyUp` and `IsKeyDown`:

- Printable keys use the **lowercase character** itself: `a`–`z`, `0`–`9`, `" "`, and
  `;` `+` (the `=` key) `,` `-` `.` `/` `` ` `` `[` `\` `]` `'`. Keypad 0, - and + map
  to the same characters.
- Named keys: `LEFTBUTTON MIDDLEBUTTON RIGHTBUTTON MOUSE4 MOUSE5 WHEELUP WHEELDOWN
  BACK TAB RETURN ESCAPE SHIFT CTRL ALT PAUSE PAGEUP PAGEDOWN END HOME PRINTSCREEN
  INSERT DELETE UP DOWN LEFT RIGHT F1`–`F15 NUMLOCK SCROLLLOCK`.
- `IsKeyDown` matches names case-insensitively. Single-character names are lowercased.
- Keypad Enter is `RETURN`. Left and right modifiers collapse into `SHIFT`/`CTRL`/`ALT`.

**macOS mapping (host decision):** ⌘ Command reports as **`CTRL`**, so that ⌘C, ⌘V,
⌘Z and ⌘S use PoB's Ctrl shortcuts. The physical Control key also maps to `CTRL`.
⌥ Option is `ALT`. On a laptop without Delete/Home/End, Fn+Backspace sends Delete as
usual.

### 4.2 Character input (`sys_video.cpp:522`, `ui_main.cpp:548`)

- Text input codepoints arrive as `OnChar`. SG pushes `"%c"` for codepoints < 128.
  **Codepoints ≥ 128 become a key name or `"?"`**, so non-ASCII typing is effectively
  broken in the original.
- In addition, on key **down** of Backspace, Tab, Enter and Escape, SG sends `OnChar`
  with `"\b"`, `"\t"`, `"\r"` and `"\27"`. `EditControl` relies on `"\b"` being ignored,
  and on Tab and Enter arriving as chars.
- **Host decision:** send the UTF-8 encoding for non-ASCII codepoints instead of `"?"`.
  `EditControl` uses `lua-utf8` and handles multi-byte strings. ASCII behaviour stays
  identical.

### 4.3 Fonts and text rendering (`r_font.cpp`)

**Font names → files** (`r_main.cpp:914`), from `<runtime>/SimpleGraphic/Fonts/`:

| PoB name | File base |
|---|---|
| `FIXED` (default for unknown) | `Bitstream Vera Sans Mono` |
| `VAR` | `Liberation Sans` |
| `VAR BOLD` | `Liberation Sans Bold` |
| `FONTIN SC` | `Fontin SmallCaps` |
| `FONTIN SC ITALIC` | `Fontin SmallCaps Italic` |
| `FONTIN` | `Fontin` |
| `FONTIN ITALIC` | `Fontin Italic` |

**`.tgf` format** (one per font), parsed line by line:

```
HEIGHT <h>;                     -- starts a size; atlas is "<base>.<h>.tga"
GLYPH <x> <y> <w> <spL> <spR>;  -- next glyph (index 0, 1, 2 … up to 127)
```

Glyph *i* is codepoint *i*, and there are at most 128 per size. Texture coordinates
are `x/texW … (x+w)/texW` and `y/texH … (y+h)/texH`. The atlases are TGA. The shipped
sizes are 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 32, 36, 40, 48, 56 and 64.

**Height selection** (`r_font.cpp:99`): for a requested height *H*, use the largest
atlas size ≤ *H*. The exception is the top `lim = (gap − 1) / 2` heights (integer
division) just below a size, which round **up** to it. For example, 31 uses 32, while
29 and 30 use 28. Heights below the smallest size use the smallest, and heights above
the largest use the largest. The chosen atlas is scaled by `scale = H / atlasHeight`.
PoB's heights reach this point already rounded to even numbers (§3.9), so most of them
hit an atlas exactly.

**Width** (`StringWidthInternal`), per line, ignoring colour escapes:
```
w = 0
for each codepoint c:
  if c >= numGlyph:  for each char of "[U+XXXX]": w = ceil(w + adv(tofuFont, ch))   -- unscaled
  elif c == '\t':    w = ceil(w + adv(' ') * 4 * scale)
  else:              w = ceil(w + adv(c) * scale)
adv(g) = g.width + g.spLeft + g.spRight
```
`DrawStringWidth` returns the maximum over the `\n`-separated lines, divided by the DPI
scale. The "tofu" font is the largest atlas at least 3 px shorter than *H*
(`FindSmallerFontHeight`).

**Drawing** (`DrawTextLine`): `y = floor(top)`, and `x` is aligned as follows:

| align | x |
|---|---|
| `LEFT` | `left` |
| `CENTER` | `floor((screenW - width)/2 + left)` (centred on the **screen**, offset by `left`) |
| `RIGHT` | `floor(screenW - width - left)` (right-aligned to the **screen**) |
| `CENTER_X` | `floor(left - width/2)` |
| `RIGHT_X` | `floor(left - width)` |

Then `x = round(x)`. Per glyph: `x += spL*scale`, draw a `w*scale × H` quad, then
`x += w*scale`, `x += spR*scale`, and `x = ceil(x)`. **Tab during drawing does not
apply `ceil`**, unlike the width calculation. That is an SG quirk we copy. Lines
advance by *H*. `screenW` here is the virtual screen width in physical pixels.

**Cursor index** (`StringCursorInternal`, `StringCursorIndex`): find the line whose
bottom (`lineY = H, 2H, …`) is ≥ `cursorY`, then walk that line with the same
advances as the width calculation. The result is the first glyph whose right edge is
≥ `cursorX`. A tab counts as two halves, so the caret snaps to the nearer side. The
glyph index is mapped back to a **byte offset** with the UTF-8 → UTF-32 index table,
and the Lua function returns `offset + 1`. An empty string returns 1.

**UTF-8 decoding** (`common.cpp:440`): standard 1–4 byte sequences. Every codepoint
records its source byte offset, used for the cursor mapping above. Invalid bytes
decode as a single unit each.

### 4.4 Colour escapes (`common.cpp:188`)

- `^0`–`^9` select palette entries. The palette is:
  `^0` black `(0,0,0)`, `^1` red `(1,0,0)`, `^2` green `(0,1,0)`, `^3` blue `(0,0,1)`,
  `^4` yellow `(1,1,0)`, `^5` magenta `(1,0,1)`, `^6` cyan `(0,1,1)`, `^7` white `(1,1,1)`,
  `^8` grey `(0.7,0.7,0.7)`, `^9` dark grey `(0.4,0.4,0.4)`.
- `^xRRGGBB` or `^XRRGGBB` sets an explicit colour (hex is case-insensitive).
- An escape sets RGB **and forces alpha to 1** for the rest of the string. The colour
  also persists **after** the call (it becomes the current draw colour, see
  `GetDrawColor`).
- Escapes take no width. A `^` that is not followed by a valid escape is drawn as a
  normal glyph.

---

## 5. Subscripts (`ui_subscript.cpp`)

`LaunchSubScript(scriptText, funcList, subList, ...args)` → `id` (light userdata) or `nil`.

- It runs `scriptText` in a **new `lua_State` on a new thread** with only
  `luaL_openlibs`. There is no SG API in that state except the names listed. The
  state has `os.exit` as a no-op.
- `funcList` and `subList` are comma-separated global names to install in the
  subscript state:
  - **func** (synchronous): the call blocks the subscript thread until the main
    thread processes it on its next frame through `OnSubCall(name, ...)`, then returns
    those return values to the subscript.
  - **sub** (asynchronous): the call is queued and delivered as `OnSubCall(name, ...)`
    on the next frame. It returns nothing to the subscript.
- Arguments, call data and return values are restricted to nil, boolean, number and
  string. SG copies strings with `strlen`, so embedded NULs truncate. We use lengths,
  which handles a superset.
- `require` in the subscript must find `lcurl.safe`, `xml`, `sha1`, `lzip` and the
  rest (§6). SG relies on the subscript inheriting the process cwd = runtime dir and
  the default `package.path`/`cpath` (`./?.lua`, `./?.dll`). **Our host registers the C
  modules in `package.preload` and adds `<runtime>/lua/?.lua` and
  `<runtime>/lua/?/init.lua` to `package.path` in every state**, so nothing depends on
  cwd.
- When the script returns, `OnSubFinished(id, ...returns)` fires on the main thread.
  On a Lua error it fires `OnSubError(id, msg)` instead.
- `AbortSubScript(id)` sets a line hook that raises an error, waits for the thread and
  discards the results. `IsSubScriptRunning(id)` returns a boolean.
- **Host requirement:** when a subscript queues a call or finishes, it must **wake the
  main loop** so the result is delivered immediately, even while the UI is idle.

PoB's subscripts, all of which run in this restricted environment:

| Caller | funcList | subList | Needs |
|---|---|---|---|
| `Launch.lua:314` `DownloadPage` | – | `ConPrintf` | lcurl |
| `Launch.lua:348` update check (`UpdateCheck.lua`) | `GetScriptPath,GetRuntimePath,GetWorkDir,MakeDir` | `ConPrintf,UpdateProgress` | lcurl, lzip, xml, sha1 |
| `PoEAPI.lua:108` OAuth (`LaunchServer.lua`) | – | `ConPrintf,OpenURL,Copy` | **socket**, dkjson |
| `BuildSiteTools.lua:50` upload | – | – | lcurl |
| `PoBArchivesProvider.lua:49` | – | – | lcurl |
| `TreeTab.lua:763` | – | – | lcurl |

The main side forwards `OnSubCall` names to `_G[name]` (`Launch.lua:211`). So the
func calls listed above run the real host functions (`GetScriptPath`, `MakeDir`, …) on
the main thread.

---

## 6. Native Lua modules

PoB `require`s these C modules. SG builds them from their upstream projects, all MIT
licensed, and so will we, statically linked into the host and registered in
`package.preload` of every state.

| `require` name | Upstream | Used by | API surface used |
|---|---|---|---|
| `lcurl.safe` | [Lua-cURLv3](https://github.com/Lua-cURL/Lua-cURLv3) | downloads, import, update | `curl.easy()`; `easy:setopt_url`, `:setopt(opt, v)`, `:setopt_writefunction(fn\|file)`, `:setopt_headerfunction`, `:setopt_useragent`, `:perform()` → `ok, err` (`err:msg()`), `:getinfo(info)`, `:getinfo_response_code()`, `:escape(s)`, `:close()`. Options: `OPT_ACCEPT_ENCODING, FOLLOWLOCATION, HTTPHEADER, IPRESOLVE, POST, POSTFIELDS, PROXY, SSL_VERIFYHOST, SSL_VERIFYPEER, USERAGENT`; info: `INFO_RESPONSE_CODE, INFO_SIZE_DOWNLOAD, INFO_REDIRECT_URL` |
| `lua-utf8` | [luautf8](https://github.com/starwing/luautf8) | `Common.lua`, `EditControl.lua` | `find, gsub, match, next, reverse, sub` (and the rest of the module) |
| `lzip` | SimpleGraphic `libs/LZip/lzip.cpp` (MIT, © David Gowor) | `UpdateCheck.lua` (only when a Source URL ends in `.zip`) | `lzip.open(path)` → `zip:OpenFile(name)` → `f:Read("*a")`, `f:Close()`, `zip:Close()`, plus `GetNumFiles/GetFileName/GetFileSize`, `f:Length` |
| `socket.core` | [LuaSocket](https://github.com/lunarmodules/luasocket) | `LaunchServer.lua` (OAuth redirect listener on localhost) | `socket.tcp`, `bind`, `listen`, `accept`, `getsockname`, `settimeout`, `receive`, `send`, `close` |

Pure-Lua modules come from `<runtime>/lua/`: `xml`, `dkjson`, `base64`, `sha1` (a
directory with `init.lua`), `sha2`, `socket` (the helper on top of `socket.core`) and
`lua-profiler`. `bit`, `jit` and `ffi` are LuaJIT built-ins.

**System CA certificates:** macOS libcurl (SecureTransport/LibreSSL) uses the system
trust store. On Windows we build curl with Schannel, which also uses the system store.

---

## 7. Globals PoB reads but never defines

A global-read vs global-write scan (bytecode) over all PoB Lua turns up these
host-provided names that are *not* defined in Lua. They are all already listed above:

`GetDrawColor`, `SetForeground`, and `UpdateProgress` (provided only inside the
update subscript as a sub; the main script guards with `if UpdateProgress`).

The remaining names in that scan (`aura`, `cfg`, `skillCfg`, `stat`, `item`, …) are
accidental globals in PoB's own code (reads of undeclared locals). They are `nil` in
the original too and must stay `nil`. **Do not provide them, and do not let the
safety net (§9) turn them into functions.**

PoB also **writes** some host names: `SetDPIScaleOverridePercent` gets a fallback if
missing (`Main.lua:125`), and `GetCursorPos` is temporarily replaced
(`CompareTab.lua:3396`). Host functions must therefore be ordinary global function
values.

---

## 8. Installation layout and the updater

### 8.1 How an official Windows install looks

`manifest.xml` and `manifest.cfg` describe four parts:

| part | repo path | installed to | Source URL |
|---|---|---|---|
| `default` | `/` (`changelog.txt`, `help.txt`, `LICENSE.md`) | script dir | `…/{branch}/` |
| `program` | `src/` (minus `Export/`, `TreeData/`, …) | script dir | `…/{branch}/src/` |
| `tree` | `src/TreeData/` | script dir | `…/{branch}/src/` |
| `runtime` | `runtime/` (exe, DLLs, `lua/`, `SimpleGraphic/Fonts/`) | runtime dir | `…/{branch}/runtime/` (`platform="win32"`) |

In an installed copy, **script dir = runtime dir = install root**: `Launch.lua`,
`Modules/`, `Path of Building.exe`, `lua/` and `SimpleGraphic/` all live side by side.
The installed `manifest.xml` has `<Version number="…" branch="master" platform="win32"/>`.
The repo copy has only `number`, which is how `Launch.lua:57` detects dev mode.

Marker files in the script dir:

- `installed.cfg`: its presence sets `launch.installedMode`. **Required for us**,
  because `Main.lua:86` puts the user path (builds, Settings.xml) in the **script
  dir** when `GetScriptPath() == GetRuntimePath()` and `installedMode` is off
  (portable mode).
- `first.run`: triggers an immediate update on the first start (`Launch.lua:29`). We
  do not ship it.

### 8.2 Our layout

| What | macOS | Windows |
|---|---|---|
| Script dir = runtime dir (writable, updated in place) | `~/Library/Application Support/PathOfBuilding-Native/` | `%APPDATA%\PathOfBuilding-Native\` (`SDL_GetPrefPath`) |
| User path (`GetUserPath()`) | `~/Documents` (PoB adds `/Path of Building/`) | Documents (same as the original) |
| Bundled PoB copy (read-only) | `PoB.app/Contents/Resources/PathOfBuilding/` | next to the exe |

On first start, or when the bundled version is newer than the installed one (an app
upgrade), the host copies the bundle into the script dir with the installed layout
from §8.1. It writes a manifest with our platform and branch, and creates
`installed.cfg`. The host binary itself is never listed in the manifest.

### 8.3 Update check (`UpdateCheck.lua`): verified behaviour

1. It reads the local `manifest.xml`: `Version{number, platform, branch}`, the
   `default` Source and all `File`s (`fullPath` = runtime dir for `part="runtime"`,
   else script dir). The check fails with "Invalid local manifest" if the version,
   source, branch or files are missing.
2. It downloads `<default source with {branch}>/manifest.xml`.
3. **File filter:** it keeps remote files where `platform` is absent or equals the
   local platform (`UpdateCheck.lua:158`). Remote Sources are keyed by
   `platform or "any"` (`:156`).
4. A file is updated if its SHA-1 differs from the local manifest entry, or if the file
   on disk is missing or its content does not match (LF and CRLF are both accepted).
   Local files absent from the remote list are **deleted**.
5. It downloads into `<script>/Update/`, from `partSources[localPlatform] or
   partSources["any"]` (`:226`).
6. It writes `Update/manifest.xml` with
   **`Version{number=remote, platform=localPlatform, branch=localBranch}`** (`:291`).
   **So our platform string survives every update.** The question in the brief is
   answered: the platform is kept, not reset to `win32`.
7. It writes `Update/opFile.txt` (`move "src" "dst"`, `delete "path"`, plus the move
   of the new manifest). If any file has `part="runtime"`, mode becomes `"basic"` and
   the runtime moves go to `Update/opFileRuntime.txt`, which ends with
   `start "<runtimeExecutable>"` (`:316`, `:328`).
8. It returns `"none"`, `"normal"` or `"basic"`. `Launch.lua:325`:
   - `normal`: `LoadModule("UpdateApply", "Update/opFile.txt")` in-process, then
     `Restart()`.
   - `basic`: same for `opFile.txt`, then
     `SpawnProcess(GetRuntimePath().."/Update", "UpdateApply.lua Update/opFileRuntime.txt")`
     and `Exit()`. On Windows, `Update.exe` is a minimal SG host that runs
     `UpdateApply.lua`, which finally `SpawnProcess`es the main exe again.

### 8.4 ⚠️ Finding: the platform filter does not match the real manifest

The brief assumes that Windows binaries carry `platform="win32"`. **They don't.**
PoB's `update_manifest.py` writes **`runtime="win32"`** on `.dll` and `.exe` entries
(`update_manifest.py:111`). Only the runtime **Source** gets `platform="win32"`. For a
host whose local platform is anything other than `win32`, that means:

1. **All 25 DLLs and the exe pass the filter** (no `platform` attribute), so they
   become update candidates. They are not in our local manifest, so they would be
   downloaded.
2. Their Source is keyed `win32` only, so `partSources[ourPlatform] or
   partSources["any"]` is `nil`, and `source:gsub` **crashes the update thread**
   ("attempt to index a nil value"). The same crash hits any runtime-part file that
   changes, such as `lua/dkjson.lua` or a font.
3. Any runtime-part change switches to `"basic"` mode. The op list then ends with
   `'start "'..runtimeExecutable..'"'`, and `runtimeExecutable` is `nil` without an
   exe entry in our manifest, which is another crash.

It "works" on Windows only because there the local platform *is* `win32`.

**Proposed host-side fix (to implement in M5; no Lua changes):** the host's `lcurl`
implementation recognises the download of PoB's own manifest (URL path ending in
`/manifest.xml` under
`raw.githubusercontent.com/PathOfBuildingCommunity/PathOfBuilding/`) and normalises
it before Lua sees it:

- `File` entries with `runtime="…"` get `platform="…"` too. The Windows binaries are
  then filtered out by PoB's own check.
- Runtime-part files without a platform (`lua/*.lua`, fonts) are moved to a host part,
  `part="runtime-native"`, and a matching `<Source part="runtime-native" url="…/runtime/"/>`
  is added. Because our script dir equals our runtime dir, those files still land in
  the right place (`fullPath = scriptPath/name`). The update stays in `"normal"` mode,
  so `SpawnProcess`/`Update.exe` and the `start` op are never needed.
- The same transformation is applied at build time to the bundled `manifest.xml`, so
  local and remote agree from the start.

As a fallback, `SpawnProcess(<runtime>/Update, "UpdateApply.lua <ops>")` is handled
internally: the host runs `UpdateApply.lua` with that op file in a bare Lua state
after `Exit()`, then relaunches itself. `SpawnProcess` of any other path launches the
executable detached, matching SG.

Platform strings: **`native-macos`** and **`native-win64`**. They are deliberately
not `macos` or `win64`, so they can never collide with anything upstream might add.

---

## 9. Safety net for unknown globals

Future PoB releases may call host functions that we have not implemented yet. With
no handling, such a call fails with "attempt to call global 'X' (a nil value)". It is
caught by PoB's `PCall` and shown as an error popup on every frame, which makes the
app unusable.

After the API is registered, the host sets a metatable on `_G` in the main state
only. Its `__index` handler works like this:

- A missing name that **looks like a host API function** (CamelCase: matches
  `^[A-Z][a-z][A-Za-z0-9]*$`, so `CONSTANTS` and `Snake_Case` are excluded) and is **not** on an allow-nil list gets a stub. The stub
  logs `[host] unimplemented API function '<name>' (first called from <file>:<line>)`
  once, using `debug.getinfo(2, "Sl")`, and returns no values. The stub is cached in
  `_G`, so the lookup happens once per name.
- Every other name returns `nil`. This keeps the accidental lowercase globals of §7
  `nil`, as in the original.
- The **allow-nil list** holds names that PoB tests for existence and that must stay
  `nil` when they are absent. Today that is `UpdateProgress` (a subscript-only sub),
  plus any name PoB reads in a guard pattern (`if X then`, `X and X()`,
  `if not X then`) that the host does not provide. A grep of v2.67.2 finds only
  `UpdateProgress`, `SetDPIScaleOverridePercent` and `GetScreenScale`, and the host
  implements the last two. `--check-boot` logs any stub created during boot, so a
  new guarded name shows up in the log.
- `__newindex` is left alone, so PoB can still define and override globals normally.

The risk: a future guarded call (`if NewApi then NewApi() end`) whose name is not on
the list yet would get the stub, and PoB would believe the feature exists. That is
acceptable, because the stub does nothing and the log line makes the gap visible.

---

## 10. Rendering in the host

- The draw list is recorded per frame in physical pixels, per `(layer, subLayer)`,
  with each quad carrying its texture, colour and viewport index. It is drawn with
  `SDL_Renderer` (Metal on macOS, Direct3D on Windows): consecutive quads that share
  texture, viewport and address mode become one `SDL_RenderGeometry` call.
- The viewport is `SDL_SetRenderViewport`, which translates and clips like SG.
  Textures use `WRAP` addressing unless loaded with `"CLAMP"`, linear filtering
  unless `"NEAREST"`, and straight alpha blending.
- Images decode to RGBA8 (stb_image for PNG/JPEG/TGA, libwebp for WebP). The upload
  happens lazily on first draw. `"ASYNC"` loads decode on a worker thread, highest
  `SetLoadingPriority` first. Until a load finishes its quads are skipped, and the
  frame is marked incomplete so the loop draws again when the worker wakes it.
- Failed loads draw SG's 8×8 grey placeholder. `"MIPMAP"` is accepted but ignored,
  since PoB does not use it.
- Measured on an M-series Mac with the tree view at 2560×1600: PoB's `OnFrame`
  takes 15–40 ms and rendering about 3 ms for ~10,000 quads. Lua is the cost to
  avoid when idle.

## 11. Deliberate deviations from SimpleGraphic

| Area | SG | Host | Why |
|---|---|---|---|
| Frame pacing | spins at vsync; sleeps 100 ms when inactive | event-driven, capped, idle ≈ 0 fps (M6) | power use |
| `OnChar` ≥ U+0080 | `"?"` or a key name | UTF-8 string | correct text input |
| ⌘ on macOS | n/a | reports as `CTRL` | native shortcuts |
| `GetFileModifiedTime` | raw platform ticks | Unix seconds (float) | portable, still ordered |
| `GetUserPath` on macOS | `$XDG_DATA_HOME` or `~/.local/share` | `~/Documents` | match the Windows convention |
| Subscript `require` | relies on cwd | `package.preload` + absolute `package.path` | threads share cwd |
| Updater manifest | as downloaded | normalised (§8.4) | upstream attribute mismatch |
| `OpenURL` | any target (`ShellExecute`) | `http`/`https` only | some URLs come from editable text |
| Leading `#` line in Lua files | skipped by the old LuaJIT | blanked by the host loader (§1.1) | newer LuaJIT parses it |
| In-window console, ImGui debug, profiling | yes | log file only | not needed |

---

## 12. Open questions (to settle in later milestones)

- **Text against Windows:** compare text pixel by pixel with the Windows build,
  especially `CENTER`/`RIGHT` alignment, which uses the screen width. On macOS the
  build list, tree, Items and Calcs views look right, but no side-by-side diff has
  been done.
- **M4:** OAuth character import needs LuaSocket to bind a localhost port. Check that
  the macOS firewall prompt does not appear for loopback-only binds.
- **M5:** confirm that the manifest normalisation keeps the SHA-1s intact, by checking
  that an update right after a fresh install reports "No update available".
- **M7:** LuaJIT on arm64 under the hardened runtime needs the
  `com.apple.security.cs.allow-jit` entitlement. Ad-hoc signing without the hardened
  runtime does not.
