# Fork releases are versioned apart from the base port

Status: accepted (2026-11-23)

Non-Visual Melee releases carry a version number of their own, `0.x` while in beta, tagged `a11y-v<version>`, and each release says which base port release it plays online with. The app version stays the base port's, from the root `VERSION` file, because two copies pair online only when their app version strings match: a fork release that changed it could no longer play against base port players. So a fork build has two versions. The app version is the base port's and is never shown as the fork's. The fork version is what players, the release page, the changelog and the launcher's update check go by.

Unreleased work lives on a `dev` branch, and `master` moves only when a release is cut, by a fast-forward to `dev`. GitHub shows `master` to visitors, so the `ACCESSIBILITY.md` and `CHANGELOG.md` they read describe the release they can download. The rolling `ci-latest` prerelease builds `dev`; a pushed tag builds a versioned release, which is never replaced. A fix that cannot wait for `dev` is committed on `master`, released as a patch, and merged back into `dev`.

Versioned releases ship the Windows zip alone until a Mac build has been play-tested; `ci-latest` keeps the Mac zips for testers. Each release zip holds `readme.html` and `changelog.html`, made from `ACCESSIBILITY.md` and `CHANGELOG.md` by pandoc in CI. The launcher's update check follows the fork's releases instead of the base port's, which costs a handful of marked lines in `updater.cpp`.

Spec: `.scratch/versioned-releases/spec.md`.

## Considered options

- **The base port's version with a suffix** (`v0.4.2-a11y.1`): it tells the online pairing at a glance, but semantic versioning sorts a suffixed version below the plain one, so the base port's updater logic and GitHub's ordering would rank `v0.4.2` above every fork release of it. The fork's own changes would also have no number of their own across base merges.
- **The fork's own number with no prefix** (`v0.1.0`): the repository already holds the base port's tags `v0.1.0-beta` to `v0.4.2`, fetched from `upstream`, so the names would clash or be easy to confuse.
- **Changing the app version for fork releases**: the simplest numbering, but it breaks online play against base port builds, which the fork promises.
- **Releasing from `master` alone, with no `dev` branch**: less to keep in step, but the documentation visitors read on GitHub's front page would describe unreleased features that the download lacks.
- **The base port's update check left as it is**: no base port lines, but a fork player would hear of each base port release and be sent to a download with no speech.
- **Python's `markdown` package for the HTML**: no install on the runner, but it needs four spaces to nest a list where GitHub takes two, so `ACCESSIBILITY.md`'s lists could come out flattened. Pandoc's GitHub-flavoured reader matches what GitHub shows.

## Consequences

- Base merges go into `dev`. A base merge that moves `VERSION` leaves the last fork release paired with the old base release until the next fork release, so a release soon after such a merge keeps fork players able to meet base port players.
- Tag builds must set `MELEE_VERSION` from `VERSION` themselves: the root `CMakeLists.txt` otherwise takes the version from a tag's name.
- `ACCESSIBILITY.md` and `CHANGELOG.md` link only by full URL, since their HTML copies are read from an unzipped folder.
