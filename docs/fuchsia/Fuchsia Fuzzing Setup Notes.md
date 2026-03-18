# 1. Downloading the Fuchsia source code (adapted from [here](https://fuchsia.dev/fuchsia-src/get-started/get_fuchsia_source))

1. First begin by installing all the required prerequisite packages:
```bash
sudo apt install curl file git unzip
```

2. Run a check to ensure that the target machine for Fuchsia doesn't have any glaring issues that would prevent Fuchsia from being built:
```bash
curl -sO https://storage.googleapis.com/fuchsia-ffx/ffx-linux-x64 && chmod +x ffx-linux-x64 && ./ffx-linux-x64 platform preflight
```

3. Determine a directory to install the Fuchsia source code to and run the following script:
```bash
curl -s "https://fuchsia.googlesource.com/fuchsia/+/HEAD/scripts/bootstrap?format=TEXT" | base64 --decode | bash
```

4. We need to now specify our environment variables (I will include environment variables we will need for later parts of the setup as well)

   First open your `.bash_profile` and insert the subsequent environment variables to it:
```bash
vim ~/.bash_profile
# Insert environment variables below into .bash_profile
source ~/.bash_profile
```

```bash

export PATH=/PATH_TO_FUCHSIA/.jiri_root/bin:$PATH
source /PATH_TO_FUCHSIA/scripts/fx-env.sh
export PATH="/PATH_TO_FUCHSIA/prebuilt/third_party/qemu/linux-x64/bin:$PATH"h
export GOROOT=/PATH_TO_GO
export PATH=/PATH_TO_GO/bin:$PATH
```

5. Verify that the following commands can be run:
```bash
jiri help
fx help
```

6. Ensure that `kvm` has read and write accesses turned on for everyone:
```bash
sudo chmod 666 /dev/kvm
```
   
---
# 2. Configuring and building Fuchsia
At one point in time, `syzkaller` was integrated with Fuchsia. However, at some point in time (roughly 7 years ago), the development of Fuchsia continued leaving the integration of syzkaller behind. As such, at build-time, there is technically an option for building Fuchsia with `syzkaller` integrated. However, this build fails and so we must point Fuchsia to an external checkout of `syzkaller`.  As such, we build Fuchsia as follows:

```bash
fx --dir "out/x64" set core.x64 \
  --with "//bundles/tools" \
  --variant=kasan \
  --include-clippy=false
```

---
# 3. Setting up `syzkaller` with Fuchsia
Assuming we've checked out the latest push of `syzkaller`, we  can run `make` with our target operating system set to Fuchsia:

```bash
make TARGETOS=fuchsia TARGETARCH=amd64
```

Interestingly, in `syzkaller`, the `Makefile` is written to *not* construct `syz-executor` (which ends up causing issues down the road as this is the primary mechanism by which `syzkaller` runs `syz-manager`). So just running `make` as above will not immediately work. Before we address this fix, we establish the configuration file that `syz-manager` takes in as input to specify the various fuzzing parameters, Zircon kernel location, private key location, Fuchsia image location, etc...

```json
{
    "target": "fuchsia/amd64",
    "http": ":12345",
    "workdir": "/proj/ecs-251-PG0/groups/fuzzing/fuchsia_fuzzing/workdir",
    "kernel_obj": "$SOURCEDIR/out/x64/kernel_x64-kasan/obj/zircon/kernel",
    "syzkaller": "/proj/ecs-251-PG0/groups/fuzzing/fuchsia_fuzzing/syzkaller",
    "image": "$SOURCEDIR/out/x64/obj/build/images/fuchsia/fuchsia_gen/fxfs.blk",
    "sshkey": "/proj/ecs-251-PG0/groups/fuzzing/fuchsia_fuzzing/workdir/pkey",
    "reproduce": false,
    "cover": false,
    "procs": 8,
    "type": "qemu",
    "vm": {
        "count": 2,
        "cpu": 4,
        "mem": 4096,
        "kernel": "$SOURCEDIR/out/x64/obj/build/images/fuchsia/product_bundle/system_a/linux-x86-boot-shim.bin",
        "initrd": "/proj/ecs-251-PG0/groups/fuzzing/fuchsia_fuzzing/workdir/fuchsia-ssh.zbi"
    }
}
```

where `$SOURCEDIR` refers to the path to `syzkaller`.  Additionally, we will need `go` in order to run `make` so we install this in the appropriate location as well:

```bash
wget https://go.dev/dl/go1.22.5.linux-amd64.tar.gz
tar -xf go1.22.5.linux-amd64.tar.gz -C /proj/ecs-251-PG0/groups/fuzzing/
export GOROOT=/proj/ecs-251-PG0/groups/fuzzing/go
export PATH=/proj/ecs-251-PG0/groups/fuzzing/go/bin:$PATH
```

We add the the bottom two lines to our `.bash_profile` so that we don't need to keep re-running them. 

## Syscalls descriptions to add to `syzkaller`
As of the most recent Fuchsia system call descriptions and the most recent checkout of syzkaller, the following syscall files do not have corresponding `.txt` files in syzkaller. Following the structure used to add other syscalls, we can ensure there are no discrepancies between the two.
- [ ] `sampler.fidl`
- [ ] `restricted.fidl`
- [ ] `iob.fidl`
- [ ] `counter.fidl`

## Trying to fuzz Fuchsia without accounting for system call definition discrepancies
Say you want to try your hand at fuzzing Fuchsia without updating any discrepancies that may exist between the system calls defined in Fuchsia and the corresponding system call descriptions (written in syzlang) in Syzkaller, this should be possible so long as the system calls in syzkaller form a subset of all the system calls currently supported by Fuchsia. If not, refer to existing Syzlang syscall definitions in the `syzkaller` Github repo and fill in the missing system call descriptions.

Assuming this has been dealt with, the next goal is to build `syz-executor`, which brings us back to the issue mentioned above.  Assuming all of the components of your configuration file are pointing to the correct kernel components, we can run the following commands:

```bash
$FUCHSIA_DIR/out/x64/host_x64/zbi \
  -o /proj/ecs-251-PG0/groups/fuzzing/workdir/fuchsia-ssh.zbi \
  $FUCHSIA_DIR/out/x64/obj/build/images/fuchsia/product_bundle/system_a/fuchsia.zbi \
  --entry "data/ssh/authorized_keys=$FUCHSIA_DIR/.ssh/authorized_keys"
```

This step is responsible for injecting the SSH keys into Fuchsia as Fuchsia requires a SSH handshake to take place for the virtual machine to function. Further we will have to make the following changes in the checkout of `syzkaller` itself:
## Syzkaller Source Modifications

### 1.1 `syzkaller/vm/qemu/qemu.go` — QEMU Configuration

**Change A: Network device (e1000 → virtio-net-pci)**

- Location: Fuchsia section's `NetDev` field
- Reason: e1000 driver crashes on Fuchsia with BTI leaks; virtio-net-pci is stable

```
// Before:
NetDev: "e1000",
// After:
NetDev: "virtio-net-pci",
```

**Change B: Network restriction (restrict=on → restrict=off)**

- Location: Line ~530, netdev argument
- Reason: `restrict=on` blocks the executor from connecting back to syz-manager on the host

```
// Before:
"user,id=net0,restrict=on,hostfwd=tcp:127.0.0.1:%v-:22"
// After:
"user,id=net0,restrict=off,hostfwd=tcp:127.0.0.1:%v-:22"
```

**Change C: Forward address (localhost → 10.0.2.2)**

- Location: Line ~651, Forward() function return value
- Reason: Inside the QEMU VM, `localhost` refers to the VM itself; `10.0.2.2` is the host via QEMU user networking

```
// Before:
return fmt.Sprintf("localhost:%v", port), nil
// After:
return fmt.Sprintf("10.0.2.2:%v", port), nil
```

---

### 1.2 `syzkaller/executor/syscalls.h` — Removed Syscall Stubs

- Location: Top of file (added)
- Reason: Modern Fuchsia has removed these syscalls; stubs prevent linker errors
- **Important**: Must be re-applied after every `make generate` since it regenerates this file

```c
#if GOOS_fuchsia
#include <zircon/syscalls.h>
#include <zircon/types.h>
static zx_status_t get_root_resource(void) { return ZX_ERR_NOT_SUPPORTED; }
static zx_status_t zx_framebuffer_get_info(zx_handle_t r, uint32_t* f, uint32_t* w, uint32_t* h, uint32_t* s) { return ZX_ERR_NOT_SUPPORTED; }
static zx_status_t zx_framebuffer_set_range(zx_handle_t r, zx_handle_t v, uint32_t l, uint32_t f, uint32_t w, uint32_t h, uint32_t s) { return ZX_ERR_NOT_SUPPORTED; }
static zx_status_t zx_ktrace_write(zx_handle_t h, uint32_t id, uint32_t a, uint32_t b) { return ZX_ERR_NOT_SUPPORTED; }
static zx_status_t zx_pager_query_dirty_ranges(zx_handle_t p, zx_handle_t v, uint64_t o, uint64_t l, void* b, size_t bs, size_t* a, size_t* av) { return ZX_ERR_NOT_SUPPORTED; }
static zx_status_t zx_pc_firmware_tables(zx_handle_t h, zx_paddr_t* at, zx_paddr_t* st) { return ZX_ERR_NOT_SUPPORTED; }
#endif
```

---

### 1.3 `syzkaller/executor/executor_runner.h` — Linux-Specific Code Guards

**Change A: `sys/resource.h` include guard**

- Location: Line ~8
- Reason: Fuchsia doesn't have this POSIX header

```c
// Before:
#include <sys/resource.h>
// After:
#if !GOOS_fuchsia
#include <sys/resource.h>
#endif
```

**Change B: `setrlimit` / `rlimit` usage guard**

- Location: Lines ~978-981
- Reason: `struct rlimit`, `setrlimit`, `RLIMIT_NOFILE` don't exist on Fuchsia

```c
#if !GOOS_fuchsia
        struct rlimit rlim;
        rlim.rlim_cur = rlim.rlim_max = 1 << 20;
        if (setrlimit(RLIMIT_NOFILE, &rlim))
                fail("setrlimit failed");
#endif
```

**Change C: Signal handler guards**

- Location: All `signal(SIGINT, ...)`, `signal(SIGTERM, ...)`, `signal(SIGPIPE, ...)` calls
- Reason: Fuchsia doesn't support POSIX signals; `signal()` returns SIG_ERR

```c
#if !GOOS_fuchsia
        if (signal(SIGINT, handler) == SIG_ERR)
                fail("signal(SIGINT) failed");
        // ... similar for SIGTERM, SIGPIPE
#endif
```

**Change D: `remove_dir` reference**

- Location: Line ~864
- Reason: `remove_dir` was defined inside `#ifdef GLOB_ALTDIRFUNC` block in files.h which is compiled out on Fuchsia
- Fix: Either moved `remove_dir` outside the ifdef, or added a Fuchsia fallback stub

**Change E: `ExecuteBinary` bypass for Fuchsia**

- Location: `ExecuteBinary()` function body
- Reason: Binary test execution uses `posix_spawnp` which doesn't exist on Fuchsia

```c
void ExecuteBinary(rpc::ExecRequestRawT& msg)
{
#if GOOS_fuchsia
        rpc::ExecResultRawT res;
        res.id = msg.id;
        res.error = "binary execution not supported on Fuchsia";
        rpc::ExecutorMessageRawT raw;
        raw.msg.Set(std::move(res));
        conn_.Send(raw);
        return;
#else
        // ... original code ...
#endif
}
```

---

### 1.4 `syzkaller/executor/files.h` — GLOB_ALTDIRFUNC Guard

- Location: Around lines 20-48
- Reason: Fuchsia uses musl libc which doesn't support GNU glob extensions (`gl_opendir`, `gl_closedir`, `gl_readdir`, `gl_stat`, `gl_lstat`, `GLOB_ALTDIRFUNC`)

```c
        glob_t buf = {};
#ifdef GLOB_ALTDIRFUNC
        buf.gl_opendir = reinterpret_cast<void* (*)(const char* name)>(opendir);
        buf.gl_closedir = reinterpret_cast<void (*)(void* dirp)>(closedir);
        buf.gl_readdir = [](void* dir) -> dirent* { ... };
        buf.gl_stat = stat;
        buf.gl_lstat = lstat;
        int res = glob(pattern.c_str(), GLOB_MARK | GLOB_NOSORT | GLOB_ALTDIRFUNC, nullptr, &buf);
#else
        int res = glob(pattern.c_str(), GLOB_MARK | GLOB_NOSORT, nullptr, &buf);
#endif
```

Also added a fallback `remove_dir` for Fuchsia:

```c
#ifndef GLOB_ALTDIRFUNC
static void remove_dir(const char* dir)
{
        rmdir(dir);
}
#endif
```

---

### 1.5 `syzkaller/executor/subprocess.h` — Full Fuchsia Replacement

- Reason: `posix_spawnp` returns `ENOSYS` (not implemented) on Fuchsia; `POSIX_SPAWN_SETPGROUP` and process groups don't exist
- The entire file was wrapped with `#if GOOS_fuchsia` / `#else` to provide a Fuchsia implementation using `fdio_spawn_etc`

**Fuchsia version (top of file):**

```c
#if GOOS_fuchsia
#include <lib/fdio/spawn.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <vector>
#include <string>

class Subprocess
{
public:
    Subprocess(const char** argv, const std::vector<std::pair<int, int>>& fds)
    {
        std::string bin_path;
        if (argv[0][0] != '/') {
            bin_path = std::string("/boot/bin/") + argv[0];
        } else {
            bin_path = argv[0];
        }

        std::vector<fdio_spawn_action_t> actions;
        for (auto pair : fds) {
            if (pair.first != -1) {
                fdio_spawn_action_t action = {};
                action.action = FDIO_SPAWN_ACTION_CLONE_FD;
                action.fd.local_fd = pair.first;
                action.fd.target_fd = pair.second;
                actions.push_back(action);
            }
        }

        char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
        uint32_t flags = FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_STDIO;
        zx_status_t status = fdio_spawn_etc(
            ZX_HANDLE_INVALID, flags, bin_path.c_str(), argv, nullptr,
            actions.size(), actions.data(), &process_, err_msg);
        if (status != ZX_OK)
            failmsg("fdio_spawn failed", "binary=%s status=%d msg=%s", bin_path.c_str(), status, err_msg);
    }

    ~Subprocess() { if (process_ != ZX_HANDLE_INVALID) KillAndWait(); }

    int KillAndWait() {
        zx_task_kill(process_);
        zx_signals_t signals;
        zx_object_wait_one(process_, ZX_PROCESS_TERMINATED, ZX_TIME_INFINITE, &signals);
        zx_info_process_t info;
        zx_object_get_info(process_, ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr);
        zx_handle_close(process_);
        process_ = ZX_HANDLE_INVALID;
        return info.return_code;
    }

    int WaitAndKill(uint64 timeout_ms) {
        zx_time_t deadline = zx_deadline_after(ZX_MSEC(timeout_ms));
        zx_signals_t signals;
        zx_status_t status = zx_object_wait_one(process_, ZX_PROCESS_TERMINATED, deadline, &signals);
        if (status == ZX_ERR_TIMED_OUT)
            zx_task_kill(process_);
        zx_object_wait_one(process_, ZX_PROCESS_TERMINATED, ZX_TIME_INFINITE, &signals);
        zx_info_process_t info;
        zx_object_get_info(process_, ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr);
        zx_handle_close(process_);
        process_ = ZX_HANDLE_INVALID;
        return info.return_code;
    }

private:
    zx_handle_t process_ = ZX_HANDLE_INVALID;
    Subprocess(const Subprocess&) = delete;
    Subprocess& operator=(const Subprocess&) = delete;
};

#else
// ... original Linux posix_spawn implementation ...
#endif
```

---

## 2. Fuchsia Build / Infrastructure Modifications

### 2.1 SSH Key Injection into ZBI

- Tool: `$SOURCEDIR/out/x64/host_x64/zbi`
- Reason: SSH keys must be in bootfs for sshd-host to accept connections

```bash
zbi -o $WORKDIR/fuchsia-ssh.zbi --replace \
  $SOURCEDIR/out/x64/obj/build/images/fuchsia/product_bundle/system_a/fuchsia.zbi \
  --entry data/ssh/authorized_keys=$SOURCEDIR/.ssh/authorized_keys \
  --entry bin/syz-executor=$WORKDIR/syz-executor
```

### 2.2 KVM Permissions

- Reason: QEMU with KVM requires `/dev/kvm` access

```bash
sudo chmod 666 /dev/kvm
```

---

## 3. Executor Cross-Compilation

The executor must be compiled from the external syzkaller source using Fuchsia's clang toolchain:

### Compile command:

```bash
cd $SOURCEDIR/out/x64

$SOURCEDIR/prebuilt/third_party/clang/linux-x64/bin/clang++ \
  -DGOOS_fuchsia=1 -DGOARCH_amd64=1 -DHOSTGOOS_linux=1 \
  -DGIT_REVISION=\"43249bac5ea7329293dfc606828db9cdec2cb392+\" \
  -D_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS \
  -D_LIBCPP_REMOVE_TRANSITIVE_INCLUDES \
  -D_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS=1 \
  -DZX_ASSERT_LEVEL=2 \
  -I$SYZKALLER/executor/_include \
  -I$SYZKALLER \
  -I../.. \
  -Ix64-novariant/gen \
  -I../../sdk/lib/fdio/include \
  -I../../src/lib/ddk/include \
  -I../../sdk/lib/async/include \
  -I../../sdk/lib/fit/include \
  -I../../sdk/lib/stdcompat/include \
  -I../../sdk/lib/fit-promise/include \
  -I../../sdk/lib/driver/runtime/include \
  -I../../sdk/lib/zbi-format/include \
  -I../../zircon/system/ulib/ddk-platform-defs/include \
  -I../../zircon/system/ulib/trace/include \
  -I../../zircon/system/ulib/trace-engine/include \
  -I../../zircon/system/ulib/zx/include \
  -I../../src/zircon/lib/zircon/include \
  -Ifidling/gen/zircon/vdso/zx/zither/legacy_syscall_cdecl \
  --sysroot=x64-novariant/gen/zircon/public/sysroot \
  --target=x86_64-unknown-fuchsia \
  -ffuchsia-api-level=4293918720 \
  -std=c++20 -fno-exceptions -fno-rtti \
  -Os -g3 \
  -Wno-deprecated -Wno-unknown-warning-option -Wno-error \
  -fdata-sections -ffunction-sections \
  -c $SYZKALLER/executor/executor.cc \
  -o syz-executor.o
```

### Link command:

```bash
$SOURCEDIR/prebuilt/third_party/clang/linux-x64/bin/clang++ \
  --target=x86_64-unknown-fuchsia \
  --sysroot=x64-novariant/gen/zircon/public/sysroot \
  -ffuchsia-api-level=4293918720 \
  -fno-exceptions -fno-rtti \
  -Wl,--gc-sections \
  -Lgen/src/zircon/lib/zircon/zircon.x86_64 \
  -Lx64-novariant-shared \
  -o $WORKDIR/syz-executor \
  syz-executor.o \
  -lfdio -lzircon
```

---
# Persistent Issues

The child `syz-executor exec` process crashes immediately after spawning with an `undefined instruction (ud2)` in libc.so. This is likely caused by `mmap` with `MAP_FIXED_EXCLUSIVE` (defined as `MAP_FIXED_NOREPLACE = 0x100000`, a Linux-specific flag) failing on Fuchsia, or by the fixed memory addresses used by `SYZ_DATA_OFFSET` being invalid in Fuchsia's address space layout.

The next step is to investigate and fix the `mmap_input()` / `os_init()` / `SYZ_DATA_OFFSET` memory setup in `executor.cc` for Fuchsia compatibility.