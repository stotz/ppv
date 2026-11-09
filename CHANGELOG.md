# Changelog

All notable changes to ppv are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[Semantic Versioning](https://semver.org/). The release workflow takes the
notes for a release from the matching section of this file.

## [Unreleased]

## [0.1.0] - 2026-11-09

First release.

### Added

- `cmd | ppv | cmd`: copies stdin to stdout and shows total, avg, p10, p50
  and p90 rate over the last minute on stderr, labelled, with columns that
  only grow so the line does not jump; redrawn in place once per interval.
  Rates are sampled every 100 ms independently of the redraw interval.
- `ppv --pid PID`: shows the read and write throughput of a running process
  (`/proc/PID/io` on Linux, `GetProcessIoCounters` on Windows).
- `-s`: statistics at the end with rate percentiles p10/p25/p50/p75/p90
  (constant-memory log histogram for runs longer than a minute, exact below;
  printed from 10 samples on), read vs. write time with their ratio and a
  `producer-bound` / `consumer-bound` / `balanced` verdict, transfer count
  and average size, and the transfer method. Exact byte counts are digit-grouped with `_`,
  durations below a second are in milliseconds.
- Options `-q`, `-f`, `-W`, `-i SEC`, `-N NAME`, `-B SIZE`; own parser without a
  getopt dependency.
- Linux: zero-copy transfer with `splice()` through an intermediate pipe,
  falling back to `read`/`write` where splicing is not possible.
- Windows: `ReadFile`/`WriteFile` on the raw handles (binary, no CRT text
  mode), console and mintty/MSYS pty detection, Ctrl-C handling.
- Man page `ppv.1`, installed with `cmake --install`.
- CMake presets, CI for Linux (gcc, clang, arm64), macOS, Windows (MSVC,
  MinGW, arm64) and FreeBSD, sanitizers, coverage on Linux and MinGW,
  clang-format, clang-tidy, CodeQL, OpenSSF Scorecard; release workflow with
  signed build provenance.

[Unreleased]: https://github.com/stotz/ppv/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/stotz/ppv/releases/tag/v0.1.0
