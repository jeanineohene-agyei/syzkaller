# syzkaller - kernel fuzzer
[![CI Status](https://github.com/google/syzkaller/workflows/ci/badge.svg)](https://github.com/google/syzkaller/actions?query=workflow/ci)
[![OSS-Fuzz](https://oss-fuzz-build-logs.storage.googleapis.com/badges/syzkaller.svg)](https://bugs.chromium.org/p/oss-fuzz/issues/list?q=label:Proj-syzkaller)
[![Go Report Card](https://goreportcard.com/badge/github.com/google/syzkaller)](https://goreportcard.com/report/github.com/google/syzkaller)
[![Coverage Status](https://codecov.io/gh/google/syzkaller/graph/badge.svg)](https://codecov.io/gh/google/syzkaller)
[![GoDoc](https://godoc.org/github.com/google/syzkaller?status.svg)](https://godoc.org/github.com/google/syzkaller)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

`syzkaller` (`[siːzˈkɔːlə]`) is an unsupervised coverage-guided kernel fuzzer.\
Supported OSes: `FreeBSD`, `Fuchsia`, `gVisor`, `Linux`, `NetBSD`, `OpenBSD`, `Windows`.

Found bugs: [Darwin/XNU](docs/darwin/README.md), [FreeBSD](docs/freebsd/found_bugs.md), [Linux](docs/linux/found_bugs.md), [NetBSD](docs/netbsd/found_bugs.md), [OpenBSD](docs/openbsd/found_bugs.md), [Windows](docs/windows/README.md).

---

## ECS 251 Project: An Exploration of Kernel-Level OS Fuzzing

This repository has been extended as part of a course project at UC Davis 
exploring kernel-level OS fuzzing across Linux, FreeBSD, and Fuchsia. 
We use syzkaller to replicate known kernel bugs and evaluate bug 
discoverability across kernel versions and OS architectures.

### Added Files and Directories

**Crash Programs and Fuzzing Results:**
- [`fuzzing-crashes-linux5.0/`](fuzzing-crashes-linux5.0/) — Results 
  from fuzzing Linux 5.0, containing:
  - `direct-repro/` — Kernel config and crash program for direct 
    reproduction of `KASAN: use-after-free Read in screen_glyph_unicode`
  - `natural-fuzzing/` — Crash reports and logs from 22-hour 
    syz-manager run, including the newly discovered 
    `KASAN: slab-out-of-bounds Read in vcs_write`

- [`fuzzing-crashes-linux5.15/`](fuzzing-crashes-linux5.15/) — Results 
  from fuzzing Linux 5.15, containing:
  - `bug-1/` — Reports, logs, and crash program for first discovered bug
  - `bug-2/` — Reports, logs, and crash program for second discovered bug

**Proposed Patch:**
- [`vcs_patch_tracer.c`](vcs_patch_tracer.c) — Modified `vcs_write` 
  function with a proposed race condition tracer (`WARN_ON` assertion) 
  and patch to revalidate the `org` pointer after console lock 
  reacquisition, addressing the `KASAN: slab-out-of-bounds Read in 
  vcs_write` bug discovered during natural fuzzing of Linux 5.0.

---

## Documentation

Initially, syzkaller was developed with Linux kernel fuzzing in mind, but now
it's being extended to support other OS kernels as well.
Most of the documentation at this moment is related to the 
[Linux](docs/linux/setup.md) kernel.
For other OS kernels check:
[FreeBSD](docs/freebsd/README.md),
[Fuchsia](docs/fuchsia/README.md).

- [How to install syzkaller](docs/setup.md)
- [How to use syzkaller](docs/usage.md)
- [How syzkaller works](docs/internals.md)
- [How to install syzbot](docs/setup_syzbot.md)
- [How to contribute to syzkaller](docs/contributing.md)
- [How to report Linux kernel bugs](docs/linux/reporting_kernel_bugs.md)
- [Tech talks and articles](docs/talks.md)
- [Research work based on syzkaller](docs/research.md)
