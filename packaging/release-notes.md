Native Path of Building for macOS (Apple Silicon) and Windows x64. It runs the
**unmodified** [Path of Building Community](https://github.com/PathOfBuildingCommunity/PathOfBuilding)
code in a lightweight native host. PoB @POB_VERSION@ is bundled, and the app
updates itself from the official repository through PoB's own "Update" button.

### macOS (Apple Silicon)

1. Download `PathOfBuilding-macos-arm64.zip`, unzip it, and move
   **Path of Building.app** to Applications.
2. The app is ad-hoc signed, not notarised, so macOS blocks the first start.
   Right-click the app, choose **Open**, then **Open** again. Or, in Terminal:
   ```
   xattr -dr com.apple.quarantine "/Applications/Path of Building.app"
   ```

PoB's files go to `~/Library/Application Support/PathOfBuilding-Native/`. Builds
and settings go to `~/Documents/Path of Building/`.

### Windows x64

Download and run **`PathOfBuilding-Setup-win64.exe`**. Like the official setup it
installs for your user only, without administrator rights (to
`%LOCALAPPDATA%\Programs\Path of Building Native`). It adds Start menu and
desktop shortcuts and an uninstaller under Settings › Apps. It installs next to
an official PoB without affecting it.

Portable alternative: extract `PathOfBuilding-win64.zip` (right-click › Extract
All) and run `PathOfBuilding.exe` in the extracted folder. Don't run it from
inside the zip. PoB's files go to `%APPDATA%\PathOfBuilding-Native\`. Builds and
settings go to `Documents\Path of Building\`, the same place the official version
uses, so both see the same builds.

### Notes

- Idle CPU use is close to zero: the UI only redraws when something changes.
  Frame rates can be tuned in `host-settings.ini` in the data folder.
- ⌘ works as Ctrl on macOS (⌘C, ⌘V, ⌘Z, ⌘S).
- This is not an official release of Path of Building. Report problems with the
  native host here, not to the PoB Community.

Host @TAG@.
