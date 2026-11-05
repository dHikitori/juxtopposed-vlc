# Build and release an add-on

The app installs a release from Git: it fetches the release tag and reads
each file that `addon.json` declares from the tree of the tagged commit. It
does not download release assets, and it does not build anything. So a
built module, such as `dist/addon.wasm`, must be in the tagged commit.

It does not have to be on your branch. The SDK's release workflow builds the
module, commits it on a commit on top of the branch head (not on the
branch), tags that commit `v<version>` and pushes only the tag. Your branch
keeps only source, and each tag has the source and the module built from it.

## The SDK repository

[hakone-org/addon-sdk](https://github.com/hakone-org/addon-sdk) is a
read-only mirror of `contrib-addons/sdk` in
[hakone-org/hakone](https://github.com/hakone-org/hakone). Send changes and
issues to hakone-org/hakone. Its tags:

| Tag | Meaning |
| --- | --- |
| `v1.2.0` | One SDK release. It never moves. |
| `v1` | The newest `v1.x.y`. Use it to get compatible fixes. |

The app repository tags the same release `sdk-v1.2.0`; the workflows build
the add-on worker from that tag.

## Use the Rust SDK crate

```toml
[dependencies]
hakone-addon-message-v1 = { git = "https://github.com/hakone-org/addon-sdk", tag = "v1.0.0" }
```

Pin an exact tag, and commit `Cargo.lock`: the release workflow builds with
`--locked`.

## Use the workflows

Check each push and pull request:

```yaml
# .github/workflows/check.yml
name: Check
on:
  push:
  pull_request:
jobs:
  check:
    uses: hakone-org/addon-sdk/.github/workflows/check.yml@v1
```

Release when the version in `addon.json` changes on `master`:

```yaml
# .github/workflows/release.yml
name: Release
on:
  push:
    branches: [master]
    paths: [addon.json]
  workflow_dispatch:
jobs:
  release:
    uses: hakone-org/addon-sdk/.github/workflows/release.yml@v1
    permissions:
      contents: write
```

Put `/dist/` in `.gitignore`. To release, change `version` in `addon.json`
and push. The workflow:

1. stops if the tag `v<version>` exists;
2. installs the Rust version that the app pins (`rust-version.txt`), runs
   `cargo test --locked` and `./build.sh`;
3. checks the package (see below), with the tag;
4. builds the add-on worker from the app source at `sdk-v<VERSION>` and runs
   `tests/test_worker.py --worker <worker>`, if your package has it;
5. tags a commit that adds the declared files that Git does not track, and
   pushes the tag;
6. makes a GitHub release for the tag. It has no files: users install from
   the repository and the tag.

Inputs for both workflows:

| Input | Default | Use |
| --- | --- | --- |
| `sdk-ref` | `v1` | The SDK ref for the tools. Set it to the ref in `uses` if you pin another one. |
| `rust-version` | the app's pin | Another Rust toolchain. |
| `worker-tests` | `true` | `false` skips `tests/test_worker.py`. |
| `runs-on` | `ubuntu-24.04` | Another runner. |

`release.yml` also has `github-release` (default `true`).

To use the steps in your own job, use the composite action after you check
out your repository:

```yaml
- uses: actions/checkout@v4
- uses: hakone-org/addon-sdk/actions/check@v1
```

A data-only package (a theme or a visualizer) has no `Cargo.toml`; the
workflows skip the build and the worker tests and still check and tag it.

## Check and release without the workflows

```sh
pip install jsonschema==4.23.0
python3 <sdk>/tools/check-package.py .               # after ./build.sh
python3 <sdk>/tools/check-package.py --source-only . # before it
python3 <sdk>/tools/release.py . --push              # tag v<version> with the built files
```

`check-package.py` applies the published schemas to the manifest, views,
scenes and settings schema, and the install rules that need no app: each
declared file exists and is within the size limits, a worker command lists
its operation, each view message is in the default locale, panel icons use
only the permitted shapes, and the repository tree has no links, no
submodules, no path that is not portable, no two paths that differ only by
case, and at most 4096 files. The app's own `vlc-addon-validate` (see
[author tools](authoring-tools.md)) and the install stay the authority.

`release.py` changes neither your branch nor your index. It refuses a tag
that exists, a checkout with changes to tracked files, and a missing
declared file.
