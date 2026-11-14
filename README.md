# Path of Building – native port

A native port of [Path of Building](https://github.com/PathOfBuildingCommunity/PathOfBuilding),
the build planner for Path of Exile, to macOS and Windows. It runs Path of Building
Community's own code, unchanged, so the calculations, the tree, the items and the
imports are the same, and it stays current through PoB's own updates. The app around
it is built for each system: it follows your Mac's keyboard shortcuts, and uses close
to no CPU or GPU while you are not using it.

Status: early, but in use. This is not an official release of Path of Building.

## Download

[Download the latest release](https://github.com/alexvikman/pob-optimized/releases/latest).

**macOS** (Apple Silicon, macOS 13 or later): download `PathOfBuilding-macos-arm64.zip`,
unzip it and move **Path of Building** to Applications. The app is not notarised by
Apple, so macOS blocks the first start: right-click the app, choose **Open**, then
**Open** again. After that it starts as usual.

**Windows** (64-bit): download and run **`PathOfBuilding-Setup-win64.exe`**. It installs
for your user only, without administrator rights, adds Start menu and desktop
shortcuts, and can be removed under Settings › Apps. It installs next to an official
Path of Building without affecting it. To run it without installing, extract
`PathOfBuilding-win64.zip` (right-click › Extract All) and start `PathOfBuilding.exe`
in the extracted folder.

## Getting started

Start the app and use it as you would use Path of Building: create a build, or import
one from a link or from your Path of Exile account under **Import/Export Build**.

Your builds are kept in `Documents/Path of Building/`, the same folder the official
Path of Building uses. If you have both installed, both see the same builds.

## Updates

There are two kinds of update, and both ask before they do anything:

- **Path of Building itself** (skills, gems, uniques, the tree, calculations) updates
  from inside the app, as it always has: when PoB says an update is available, click
  **Update**. You don't need a new release of this app for that.
- **This app** checks once a day whether a newer release is out, and shows a small
  notice in the bottom-right corner with a **Download** button. **×** hides it until
  the next version. Installing a new release keeps your builds and settings, and never
  replaces a newer Path of Building with an older one.

## Good to know

- On macOS, **⌘** works where Path of Building uses Ctrl: ⌘C, ⌘V, ⌘Z, ⌘S and so on.
- The app only redraws when something on screen changes. Left alone it uses close to
  no CPU or GPU, and next to nothing while it is in the background.
- Signing in to your Path of Exile account shows a red warning on pathofexile.com
  that the request can't be verified. Path of Exile shows that for every desktop app
  that signs in this way, the official Path of Building too. If you started the sign-in
  from the app yourself, **Authorize** is safe.

## Settings and files

Path of Building's own settings are under **Options** in the app. A few settings for
the app around it are in `host-settings.ini`, with comments, in the app's data folder:

- macOS: `~/Library/Application Support/PathOfBuilding-Native/`
- Windows: `%APPDATA%\PathOfBuilding-Native\`

There you can change the frame rate cap and how the app paces itself while idle, or
turn off the daily check for new releases (`checkForAppUpdates = false`). Delete the
file to get the defaults back. The same folder has Path of Building's files and
`host.log`, which is useful to include when you report a problem.

When you uninstall on Windows, you can choose to remove this folder too. Your builds
in Documents are never removed.

## Problems

Report problems with this app in this repository's
[issues](https://github.com/alexvikman/pob-optimized/issues), not to the Path of
Building Community, and include `host.log` if you can.

## For developers

Building from source, the self tests, how the app runs Path of Building, and how
releases are made: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## License

MIT; see [LICENSE](LICENSE). Path of Building and SimpleGraphic are also MIT licensed
(© David Gowor and the PoB Community); see [LICENSE-THIRD-PARTY.md](LICENSE-THIRD-PARTY.md).
