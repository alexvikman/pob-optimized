# Packaging and releasing

## Packages

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
administrator rights. It adds Start menu and desktop shortcuts and an uninstaller
under Settings › Apps. The exe links the C runtime statically, and CI fails if it
depends on `vcruntime`/`msvcp` DLLs.

The macOS app is ad-hoc signed, not notarised. When it is downloaded from the
internet, macOS blocks the first start: right-click the app and choose **Open**, or
clear the quarantine flag:

```bash
xattr -dr com.apple.quarantine "/Applications/Path of Building.app"
```

## CI

GitHub Actions (`.github/workflows/build.yml`) builds both platforms on every push,
runs the unit tests and the `--check-boot` self test on the packaged app, installs,
runs and uninstalls the Windows setup, and uploads the `.app` zip, the Windows setup
and the Windows zip as artifacts.

`tools/ci-wait.sh <commit-or-tag> [artifact dest]` waits for the run of a commit or
tag, prints each job's result and can download an artifact.

The workflow builds the files users download, so every action in it is pinned to a
commit, with the release in a comment. Dependabot (`.github/dependabot.yml`) opens a
pull request when an action has a newer release.

## Making a release

```bash
tools/update-pob.sh          # bundle PoB's latest release (commit if it changed)
# set VERSION in CMakeLists.txt, commit, then:
git tag -s vX.Y.Z -m vX.Y.Z && git push origin main vX.Y.Z
```

The tag must match `VERSION` in `CMakeLists.txt`. It builds both platforms and creates
a **draft** GitHub release with both packages, their checksums in `SHA256SUMS.txt` and
the notes from `packaging/release-notes.md`. Add what is new at the top of the notes,
review the draft on GitHub, then publish it.

Each package also gets a build attestation: a signed statement, kept by GitHub, that
this exact file was built by this workflow from the tagged commit. The packages are
not signed with a paid certificate, so this is how a download can be checked:

```bash
gh attestation verify PathOfBuilding-macos-arm64.zip --repo alexvikman/pob-optimized
```

The release job refuses a draft that bundles an older PoB than PoB's latest release.
Users' installed PoB content is never replaced by an older one either way; bundling
the latest only spares new installs an update on first start.

A new PoB release does not need a release of this app: installed apps update PoB
through PoB's own "Update" button. Bundling it in the next release is enough.
