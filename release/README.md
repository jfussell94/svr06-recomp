# Releases

One repo, one release per version tag (`v0.1.0`). A release contains:

| Asset | What |
|---|---|
| `SvR06Launcher-<version>-win64.exe` | The launcher app (PyInstaller, single file) |
| `SvR06-tools-<version>.zip` | Scripts, `regions/`, `overrides/`, runtime sources, docs - everything needed to build from your own disc |
| `SHA256SUMS.txt` | Checksums |

Releases are created as drafts, assets uploaded, then published by the workflow (the repository has immutable releases: once a release exists its tag cannot be reused or its assets changed - bump the version for a fix). Tags with a hyphen (`v0.9.0-alpha.5`) are marked pre-release.

Region builds are not shipped as binaries: they contain code translated from the game, so each user recompiles from their own disc with the scripts.

## Cutting a release
1. Update `CHANGELOG.md`; commit.
2. `git tag v0.1.0 && git push --tags` - `.github/workflows/release.yml` builds the launcher and attaches the assets.
3. Local dry run: `python release/package_release.py --version 0.1.0` (writes `dist/`).

## Adding a region
Add a folder under `regions/`, an entry in `regions/regions.json`, and an entry in the launcher `BUILDS` list. No fork needed.
