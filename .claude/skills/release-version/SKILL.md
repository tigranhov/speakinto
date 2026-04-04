---
description: Build, package, and publish a new GitHub release with ZIP and installer assets. Use when the user asks to release, publish, or ship a new version.
disable-model-invocation: true
allowed-tools: Bash Read Glob Grep
argument-hint: "[version-bump: patch|minor|major]"
---

# Release Version

Build, package, and publish a new release of Wisper Agent to GitHub.

## Step 1: Determine version

1. Get the latest release tag: `gh release list --limit 1`
2. Parse the current version (format: `vMAJOR.MINOR.PATCH`)
3. Increment based on `$ARGUMENTS` or default to `patch`:
   - `patch` (default): bug fixes, hotkey changes, small improvements
   - `minor`: new features (e.g., new UI, new audio backend, new model support)
   - `major`: breaking changes or full rewrites
4. Confirm the new version with the user before proceeding

## Step 2: Update version in installer

Update `AppVersion` in `installer.iss` to match the new version (without the `v` prefix).

## Step 3: Build

```
taskkill /f /im wisper-agent.exe 2>/dev/null
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Verify the build succeeds with no errors.

## Step 4: Package ZIP

```
rm -rf release/wisper-agent
mkdir -p release/wisper-agent/assets/icons
cp build/Release/wisper-agent.exe release/wisper-agent/
cp bin/Release/whisper-cli.exe release/wisper-agent/
cp bin/Release/whisper.dll release/wisper-agent/
cp bin/Release/ggml.dll release/wisper-agent/
cp bin/Release/ggml-cpu.dll release/wisper-agent/
cp bin/Release/ggml-base.dll release/wisper-agent/
cp assets/icons/*.ico release/wisper-agent/assets/icons/
```

Create ZIP using PowerShell:
```
cd release
powershell -Command "Compress-Archive -Path 'wisper-agent' -DestinationPath 'wisper-agent-win64.zip' -Force"
```

## Step 5: Build installer

```
"$LOCALAPPDATA/Programs/Inno Setup 6/ISCC.exe" installer.iss
```

This produces `release/wisper-agent-setup-win64.exe`.

## Step 6: Generate changelog

1. Run `git log --oneline <previous-tag>..HEAD` to get commits since last release
2. If previous tag doesn't exist as a ref, find its commit: `gh release view <previous-tag> --json targetCommitish`
3. Write a changelog grouped by type:
   - **Features** — new capabilities
   - **Fixes** — bug fixes
   - **Changes** — behavioral changes, refactors
4. Each entry should describe the user-facing impact, not implementation details
5. Present the changelog to the user for approval before publishing

## Step 7: Commit, push, and publish

1. If `installer.iss` was modified (version bump), commit it:
   ```
   git add installer.iss
   git commit -m "Bump version to vX.Y.Z"
   ```
2. Push to origin: `git push origin main`
3. Create the GitHub release:
   ```
   gh release create vX.Y.Z \
     release/wisper-agent-win64.zip \
     release/wisper-agent-setup-win64.exe \
     --title "vX.Y.Z — <short title>" \
     --notes "<changelog from Step 6>"
   ```
4. Report the release URL to the user

## Release assets checklist

Every release MUST include both:
- `wisper-agent-win64.zip` — portable ZIP (all exe + DLLs + icons)
- `wisper-agent-setup-win64.exe` — Inno Setup installer

## Notes

- Never publish a release without building fresh from the current HEAD
- Always verify build succeeds before packaging
- The installer script (`installer.iss`) sources files from `build/Release/` and `bin/Release/` — these paths must exist
- `bin/Release/` contains pre-built whisper.cpp binaries that are NOT built by cmake — they are checked in / placed manually
