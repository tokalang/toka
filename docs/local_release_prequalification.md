# Local Release Prequalification

Run this before dispatching the GitHub four-target qualification. It executes
the same thirteen-stage `release_gate.py` from a fresh local clone of an exact
committed revision, with the requested release-version override. It therefore
cannot accidentally pass because of the caller's uncommitted source or a stale
`build/` directory.

It is a preflight, not release evidence. GitHub Actions remains the only
qualification authority for the exact Linux/macOS four-target matrix.

## RC4 example

From the repository root, first choose an immutable candidate SHA:

```sh
python3 tools/scripts/prequalify_release.py \
  --revision 1fb31141251f76fe41f4385dbcf05c60146c1aca \
  --version v1.0.0-rc.4 \
  --target native \
  --target linux-arm64 \
  --target linux-x64
```

The command writes individual gate reports, per-stage logs, and
`local-release-prequalification-summary.json` to
`build/local-release-prequalification/`. A failed target does not suppress the
other selected targets, so the summary is useful for diagnosis.

`native` means the current macOS or Linux architecture. On an Apple Silicon
Mac it covers macOS ARM64. The two Linux targets use Docker Desktop with the
CI-equivalent Ubuntu base and LLVM 20 dependency installation:

| Requested target | Local executor | CI row it preflights |
|---|---|---|
| `native` on Apple Silicon | native macOS | `macos-arm64` |
| `linux-arm64` | Docker `linux/arm64` | `linux-arm64` |
| `linux-x64` | Docker `linux/amd64` emulation | `linux-x64` |

macOS x64 cannot be qualified faithfully from an ARM machine; the GitHub
Intel runner remains its required evidence. Docker x64 is an effective
compiler/package preflight but is not a substitute for that runner either.

The native macOS preflight deliberately does not install dependencies. It
requires `brew install llvm@20 lld@20 zstd openssl@3`, matching the release
workflow. Docker creates its own isolated image and may download packages on
the first use. Docker gates default to `--docker-cores 2` so ASan compilation
fits Docker Desktop's normal memory allocation; raise it only after allocating
enough memory to Docker Desktop.

Use `--dry-run` to inspect the isolated-clone, CMake, and gate commands without
building or requiring Docker/Homebrew:

```sh
python3 tools/scripts/prequalify_release.py --dry-run \
  --target native --target linux-arm64 --target linux-x64
```

The runner never modifies the checkout from which it is invoked. It clones the
selected commit into a temporary directory and removes that clone after the
run; retained reports and logs stay under `--output-dir`.
