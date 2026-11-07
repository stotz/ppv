# ppv – portable pipe viewer

<img src="docs/ppv.png" align="right" width="200" alt="">

[![CI](https://github.com/stotz/ppv/actions/workflows/ci.yml/badge.svg)](https://github.com/stotz/ppv/actions/workflows/ci.yml)
[![CodeQL](https://github.com/stotz/ppv/actions/workflows/codeql.yml/badge.svg)](https://github.com/stotz/ppv/actions/workflows/codeql.yml)
[![OpenSSF Scorecard](https://api.scorecard.dev/projects/github.com/stotz/ppv/badge)](https://scorecard.dev/viewer/?uri=github.com/stotz/ppv)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

Shows how fast data flows through a pipe, or through a running process.
No progress bar, no ETA: ppv cannot know the final size of a stream, so it
does not pretend to.

ppv exists because `pv` has no native Windows build: one static binary that
behaves the same on Linux, macOS, the BSDs and Windows (console, PowerShell,
git-bash), with no dependencies, zero-copy transfer where the kernel offers
it, and only the options a pipe can actually answer.

```sh
tar cf - data | ppv -s | nc host 30000     # copy stdin to stdout, show throughput
ppv --pid 1234 -s                           # watch the I/O of a running process
```

Live line on stderr (redrawn once per second, `\r` only):

```
ppv[26.2G, avg 190M/s, p10 53.7M/s, p50 184M/s, p90 407M/s]   total; rates over the last minute
ppv[R 0B avg 0B/s | W 3.84G avg 1.11G/s]                       --pid: per direction
```

`avg` is bytes divided by time over the last minute. p10, p50 and p90 are
percentiles of the 100 ms samples of that minute: a tenth of the time it was
slower than p10, half the time slower than p50, a tenth of the time faster
than p90. Percentiles instead of min and max because a single 100 ms hiccup
would pin `min` to 0 B/s for a whole minute, while a consumer that stalls a
tenth of the time does show up in p10. The rates are sampled every 100 ms
whatever `-i` says. Each number is right-aligned to the widest value it has
shown, so the line does not jump around. `-N` replaces the `ppv` label.

Statistics with `-s` at the end:

```
ppv: 26.2G (28_168_945_664 bytes) in 2m14s, avg 200M/s, 1340 samples
  rate     p10 ~53.7M/s, p25 ~120M/s, p50 ~184M/s, p75 ~300M/s, p90 ~432M/s
  time     read 21.1s (16%), write 1m53s (84%): consumer-bound (5.3:1)
  io       26864 transfers, avg 1.00M, ReadFile/WriteFile
```

* `rate`: percentiles of the 100 ms samples over the whole run; p25 and p75
  add the interquartile range, a measure of how bursty the stream was. For
  runs up to a minute they are exact and identical to the live line; longer
  runs use a log histogram with about 6 % resolution, marked `~`. With fewer
  than 10 samples (one second) the line is left out; there is no point in a
  p90 of three values. The sample count is on the first line.
* `time`: how long ppv spent inside its read and write calls, i.e. waiting
  for the producer and for the consumer. A filter has no work of its own, so
  the two shares add up to the run time; the ratio in brackets is what
  matters. `producer-bound`: the input side is slower; `consumer-bound`: the
  output side; `balanced`: neither side accounts for half of the time.
* `io`: number of read/write cycles and their average size – the write size
  of the producer, in effect – and how the bytes were moved.

## Options

| Option | |
|---|---|
| `-s`, `--stats` | statistics at the end |
| `-q`, `--quiet` | no live line |
| `-f`, `--force` | live line even if stderr is not a terminal |
| `-W`, `--wait` | nothing is shown before the first byte arrives |
| `-i`, `--interval SEC` | redraw interval, 0.1–60 s in steps of 0.1 (default 1); sampling is always 100 ms |
| `-N`, `--name NAME` | label for the output instead of `ppv` (at most 128 characters) |
| `-B`, `--buffer-size SIZE` | transfer buffer, 4K–256M (default 1M) |
| `-p`, `--pid PID` | watch a process instead of copying |

Exit status: 0 ok, 1 error or output closed early, 2 usage error,
130 interrupted (Ctrl-C), 143 terminated (SIGTERM). Details: `man ppv`
([docs/ppv.1](docs/ppv.1)).

## Install

Binaries for Linux (x64, arm64), macOS (universal) and Windows (x64, arm64)
are on the [releases page](https://github.com/stotz/ppv/releases); each
archive contains `ppv`, the man page, `README.md`, `CHANGELOG.md` and
`LICENSE`. Unpack and put `ppv` on your `PATH`:

```sh
tar xzf ppv-0.1.0-linux-x64.tar.gz
sudo install -m 755 ppv-0.1.0-linux-x64/ppv /usr/local/bin/
sudo install -m 644 ppv-0.1.0-linux-x64/ppv.1 /usr/local/share/man/man1/
```

Windows: unzip `ppv-0.1.0-windows-x64.zip` and copy `ppv.exe` to a directory
on `%PATH%`. It runs in cmd, PowerShell, Windows Terminal and git-bash:

```sh
tar cf - data | ppv -s | ssh host "cat > data.tar"     # git-bash, same as on Linux
C:\Windows\System32\tar.exe -cf - C:\data | ppv -s > \\host\share\data.tar   # cmd
```

Run pipelines in cmd or git-bash. Windows PowerShell 5.1 does not pass bytes
between native commands: `|` decodes the stream into strings and `>` is
`Out-File`, which waits for line ends, so binary data arrives re-encoded or
not at all. PowerShell 7.4 and newer pass bytes through. Note also that git
installs its own (MSYS) `tar`; the native `C:\Windows\System32\tar.exe` is
several times faster when writing large records into a pipe.

## Build

Requires CMake ≥ 3.21 and a C17 compiler (gcc, clang, MinGW-w64, MSVC). No
other dependencies, no configure script: CMake is the configure step.

```sh
cmake --preset release && cmake --build --preset release && ctest --preset release
cmake --install build/release --prefix /usr/local
```

Without Ninja: `cmake -S . -B build && cmake --build build && ctest --test-dir build`.

Options: `PPV_BUILD_TESTS` (ON), `PPV_WARNINGS_AS_ERRORS` (OFF; ON in the
`debug` preset), `PPV_STATIC` (ON: the MinGW/MSVC binary needs only
`kernel32` and the Windows UCRT).

Contributing, code style and the test layout: [.github/CONTRIBUTING.md](.github/CONTRIBUTING.md).

## Design

```
src/
  main.c         orchestration: transfer thread + sampling/display loop
  transfer.c     copy loop; measures time blocked on input vs. output
  options.c      command line (own parser, no getopt dependency)   \
  meter.c        1-minute sliding window, whole-run stats (log histogram) |
  format.c       105M / 12.3M/s / 6m23s                                 | portable core,
  display.c      the \r status line (rendering only)                     | unit tested
  report.c       -s statistics (rendering only)                         /
  platform/
    platform.h   the only OS interface
    posix/       Linux, BSD, macOS
    win32/       Windows 7+ (built and tested on current Windows; 7 is untested)
tests/
  test_*.c       unit tests of the core; test_proc, test_pid, test_posix
                 drive the platform layer and the binary as child processes
  e2e.cmake      end-to-end tests through real pipes and files
docs/
  ppv.1          man page
  ppv.png        logo (README only, not part of the release archives)
```

The transfer thread only moves data and bumps atomic counters; the main thread
samples them every 100 ms and redraws every `-i` interval. A blocked reader or
writer never freezes the display, and no `select()` on pipes is needed (it
does not exist for pipes on Windows). Rates live in a one-minute ring buffer
(avg, p10, p50, p90 for the live line, exact percentiles for the report
while the run fits) and a 496-bucket log histogram (percentiles over longer
runs in constant memory).

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
* On Linux the last 100 ms before a watched process exits are lost
  (`/proc` is gone); Windows still delivers the final counters. The `avg`
  over the run can therefore be a little below the sampled rates.
* Exact byte counts are printed with `_` between groups of three digits
  (`930_086_912`); every modern number parser accepts that, so the value
  survives copy and paste.
* Closed standard descriptors are an error (`ppv <&-` exits with 1), they are
  never silently reused.
* On POSIX, Ctrl-C and SIGTERM end ppv by re-raising the signal after the
  statistics, so `$?` is 130 or 143 and the shell treats it like any killed
  filter.

## Releases

Pushing a tag `vX.Y.Z` that matches `project(VERSION ...)` in `CMakeLists.txt`
and has a section in [CHANGELOG.md](CHANGELOG.md) builds and tests binaries
for Linux (x64, arm64), macOS (universal) and Windows (x64, arm64) and
publishes them with SHA256 checksums and the changelog section as release
notes:

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
