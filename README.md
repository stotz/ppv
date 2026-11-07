# ppv – portable pipe viewer

[![CI](https://github.com/stotz/ppv/actions/workflows/ci.yml/badge.svg)](https://github.com/stotz/ppv/actions/workflows/ci.yml)
[![CodeQL](https://github.com/stotz/ppv/actions/workflows/codeql.yml/badge.svg)](https://github.com/stotz/ppv/actions/workflows/codeql.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

Shows how fast data flows through a pipe, or through a running process.
No progress bar, no ETA: ppv cannot know the final size of a stream, so it
does not pretend to.

```sh
tar cf - data | ppv -s | nc host 30000     # copy stdin to stdout, show throughput
ppv --pid 1234 -s                           # watch the I/O of a running process
```

Live line on stderr (updated once per second, `\r` only):

```
[105M, 12.3M/s, 45.6M/s, 98.7M/s]          total, min, avg, max over the last minute
[R 0B, 0B/s, 0B/s, 0B/s | W 3.84G, 1.06G/s, 1.11G/s, 1.14G/s]   --pid
```

Statistics with `-s` at the end:

```
ppv: 20.0M (20971520 bytes) in 3.1s, avg 6.51M/s
  rate     min 0B/s, p50 0B/s, p90 62.0K/s, max 64.0K/s (3 x 1.0s)
  waiting  input 0.0s (1%), output 3.0s (99%): the consumer (output side) is the bottleneck
  io       389 transfers, avg 52.6K, splice
```

`waiting` is the time ppv was blocked reading (producer too slow) or writing
(consumer too slow) – it tells you which side of the pipe limits the speed.

## Options

| Option | |
|---|---|
| `-s`, `--stats` | statistics at the end |
| `-q`, `--quiet` | no live line |
| `-f`, `--force` | live line even if stderr is not a terminal |
| `-W`, `--wait` | nothing is shown before the first byte arrives |
| `-i`, `--interval SEC` | update interval, 0.1–60 s (default 1) |
| `-N`, `--name NAME` | prefix for the output |
| `-B`, `--buffer-size SIZE` | transfer buffer, 4K–256M (default 1M) |
| `-p`, `--pid PID` | watch a process instead of copying |

Exit status: 0 ok, 1 error or output closed early, 2 usage error, 130 interrupted (Ctrl-C).

## Build

Requires CMake ≥ 3.20 and a C17 compiler (gcc, clang, MinGW-w64, MSVC). No
other dependencies.

```sh
cmake --preset release && cmake --build --preset release && ctest --preset release
cmake --install build/release --prefix /usr/local
```

Without Ninja: `cmake -S . -B build && cmake --build build`.

Options: `PPV_BUILD_TESTS` (ON), `PPV_WARNINGS_AS_ERRORS` (OFF; ON in the
`debug` preset), `PPV_STATIC` (ON: the MinGW/MSVC binary needs only
`kernel32` and the Windows UCRT).

## Design

```
src/
  main.c         orchestration: transfer thread + sampling/display loop
  options.c      command line (own parser, no getopt dependency)
  transfer.c     copy loop; measures time blocked on input vs. output
  meter.c        1-minute sliding window, whole-run stats (log histogram)
  format.c       105M / 12.3M/s / 6m23s
  display.c      the \r status line
  report.c       -s statistics
  platform/
    platform.h   the only OS interface
    posix/       Linux, BSD, macOS
    win32/       Windows 7+
```

The transfer thread only moves data and bumps atomic counters; the main thread
samples them each interval. A blocked reader or writer never freezes the
display, and no `select()` on pipes is needed (it does not exist for pipes on
Windows).

| | Linux | Windows |
|---|---|---|
| copy | `splice()` stdin → pipe → stdout (zero copy), falls back to `read`/`write` | `ReadFile`/`WriteFile` on the raw handles (binary, no CRT text mode) |
| terminal | `isatty`, foreground process group | console, or mintty/MSYS pty pipe |
| `--pid` | `/proc/PID/io` (`rchar`/`wchar`) | `GetProcessIoCounters` |
| time | `CLOCK_MONOTONIC` | `QueryPerformanceCounter` |

## Notes

* `--pid` counts all I/O of the process: files, pipes and sockets.
* Linux needs a kernel with I/O accounting (`CONFIG_TASK_IO_ACCOUNTING`, standard
  on distribution kernels; missing on WSL2). Other POSIX systems: not supported.
* git-bash/MSYS: `$!` is an MSYS PID; ppv needs the Windows PID
  (`ps -W`, `cat /proc/PID/winpid`).
* On Linux the last interval before a watched process exits is lost
  (`/proc` is gone); Windows still delivers the final counters.

## Releases

Pushing a tag `vX.Y.Z` that matches `project(VERSION ...)` in `CMakeLists.txt`
builds and tests binaries for Linux (x64, arm64), macOS (universal) and
Windows (x64, arm64) and publishes them with SHA256 checksums as a GitHub
release:

```sh
git tag -s v0.1.0 -m "ppv 0.1.0" && git push origin v0.1.0
```

Every archive carries a signed build provenance attestation. To check that a
download was built by this repository's release workflow:

```sh
gh attestation verify ppv-0.1.0-windows-x64.zip -R stotz/ppv
sha256sum -c SHA256SUMS --ignore-missing
```

## License

MIT – see [LICENSE](LICENSE).
