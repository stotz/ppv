# Contributing

Bug reports and pull requests are welcome. ppv is deliberately small; new
options need a reason that a plain `pv` user would recognise.

## Build and test

```sh
cmake --preset debug          # Ninja, -Werror
cmake --build --preset debug
ctest --preset debug
```

Without Ninja: `cmake -S . -B build -DPPV_WARNINGS_AS_ERRORS=ON && cmake --build build && ctest --test-dir build`.

Every change must keep the whole test suite green on Linux, macOS and
Windows (MSVC and MinGW); CI runs all of them plus sanitizers, clang-tidy,
CodeQL and a FreeBSD build.

## Code style

* C17, no compiler extensions, no dependencies beyond the C runtime and the
  platform API used in `src/platform/`.
* Formatting is enforced with clang-format 18 (`.clang-format`):
  `clang-format-18 -i src/*.c src/*.h src/platform/*.h src/platform/*/*.c tests/*.c tests/*.h`
* `clang-tidy -p build src/*.c src/platform/posix/*.c tests/*.c` must be clean
  (`.clang-tidy`).
* The portable core (`src/*.c` except `main.c` and `transfer.c`) must not
  include any OS header; everything OS specific goes through
  `src/platform/platform.h`.
* Comments in English, no emojis.

## Tests

* Pure code gets a unit test in `tests/test_*.c` (plain C, `tests/test.h`).
* Behaviour of the binary gets an end-to-end test in `tests/e2e.cmake`
  (portable) or in `tests/test_posix.c` / `tests/test_pid.c` (process
  control: descriptors, signals, non-blocking pipes, `--pid`).
* Coverage is measured on Linux (gcc) and MinGW in CI; error paths that
  need fault injection (out of memory, thread creation) are not tested.
* A bug fix comes with a test that fails without the fix.

## Pull requests

* One topic per pull request, with a short description of the why.
* Add a line to the `[Unreleased]` section of `CHANGELOG.md`.
* Commits carry a real name and e-mail address. By submitting a pull request
  you agree that your contribution is licensed under the MIT license of this
  repository.

## Releases

Maintainer only: bump `project(VERSION ...)` in `CMakeLists.txt`, move the
`[Unreleased]` entries to a new version section in `CHANGELOG.md`, then
`git tag -s vX.Y.Z -m "ppv X.Y.Z" && git push origin vX.Y.Z`. The release
workflow builds, tests and publishes the binaries.
