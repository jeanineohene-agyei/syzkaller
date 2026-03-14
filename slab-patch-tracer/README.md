# vcs_write Race Condition Tracer and Patch

## Overview

During natural fuzzing of Linux 5.0, syzkaller discovered a 
`KASAN: slab-out-of-bounds Read in vcs_write` bug that was marked 
non-reproducible. This file contains a modified version of `vcs_write` 
from `drivers/tty/vt/vc_screen.c` with a proposed race condition tracer 
and patch.

## Background

`vcs_write` temporarily releases the console lock mid-loop to safely 
copy data from userspace:

```c
console_unlock();
ret = copy_from_user(con_buf, buf, this_round);
console_lock();
```

During this window, a concurrent task can call `vc_do_resize`, shrinking 
the screen buffer. When `vcs_write` reacquires the lock, the pointer 
`org` may now point beyond the new buffer boundary, triggering an 
out-of-bounds read in `vcs_scr_readw`.

## Changes

### Race Condition Tracer (`[RC TRACER]`)
A `WARN_ON` assertion is inserted before the call to `vcs_scr_readw` 
to check whether `org` is still within the current buffer bounds:

```c
WARN_ON(org >= (u16 *)vc->vc_origin + vc->vc_screenbuf_size / 2);
```

This does **not** fix the bug. It causes the kernel to emit a warning 
with a full stack trace whenever the race window is hit, confirming 
the non-deterministic nature of the crash.

### Proposed Patch (`[PATCH]`)
After reacquiring the console lock, `org` is revalidated and recomputed 
via `screen_pos` to ensure it points within the current buffer bounds:

```c
size = vcs_size(inode);
if (size < 0 || pos >= size)
    break;
org0 = org = screen_pos(vc, p/2, viewed); /* recompute */
```

## Location of Original File

The original `vcs_write` function is located at:
```
drivers/tty/vt/vc_screen.c
```
in the Linux 5.0 kernel source tree.

## Note

This patch addresses the race condition within `vcs_write` specifically. 
Slab-out-of-bounds errors of this class have been observed across 
multiple unrelated kernel subsystems, suggesting a more comprehensive 
kernel-wide fix may be warranted. See the project paper for full discussion.
