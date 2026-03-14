# Linux 5.0 + Syzkaller Setup Guide

**CloudLab Version**

- Host OS: Ubuntu 22.04
- Target: Linux 5.0 with KCOV + KASAN
- VM: QEMU (TCG mode, no KVM)

---

## 1. Install Host Dependencies

```bash
sudo apt update
sudo apt install docker.io qemu-system-x86 debootstrap git
sudo usermod -aG docker $USER
```

Log out and log back in after adding docker group.

---

## 2. Build Linux 5.0 in Docker

Start an Ubuntu 18.04 Docker container:

```bash
docker run -it ubuntu:18.04 /bin/bash
```

Inside the container, install build dependencies and clone the kernel:

```bash
apt update
apt install -y build-essential bc flex bison \
  libssl-dev libelf-dev libncurses-dev git

cd /root
git clone --branch v5.0 \
  https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git linux-5.0
cd linux-5.0

make defconfig
make kvm_guest.config

scripts/config \
  --enable KCOV \
  --enable KCOV_ENABLE_COMPARISONS \
  --enable KASAN \
  --enable KASAN_INLINE \
  --enable DEBUG_INFO_DWARF4 \
  --enable CONFIGFS_FS \
  --enable SECURITYFS \
  --disable RANDOMIZE_BASE

make olddefconfig
make -j$(nproc)
```

---

## 3. Copy Kernel Files to Host

From a second terminal on the host, find the container ID:

```bash
docker ps -a
```

Copy the kernel files out:

```bash
docker cp <container_id>:/root/linux-5.0/arch/x86/boot/bzImage \
  /proj/ecs-251-PG0/$USER/linux-5.0-bzImage

docker cp <container_id>:/root/linux-5.0/vmlinux \
  /proj/ecs-251-PG0/$USER/vmlinux
```

---

## 4. Install Go and Build Syzkaller

### Install Go

```bash
cd /proj/ecs-251-PG0/$USER

wget https://dl.google.com/go/go1.23.6.linux-amd64.tar.gz
tar -xf go1.23.6.linux-amd64.tar.gz

export GOROOT=/proj/ecs-251-PG0/$USER/go
export PATH=$GOROOT/bin:$PATH
```

### Clone and Build Syzkaller

```bash
cd /proj/ecs-251-PG0/$USER
git clone https://github.com/google/syzkaller.git
cd syzkaller
make
# if that doesn't work, try:
make TARGETOS=linux TARGETARCH=amd64
```

### Create Debian VM Image

```bash
./tools/create-image.sh bullseye
```

### Configure Syzkaller

Create the config file:

```bash
nano /proj/ecs-251-PG0/$USER/syzkaller/linux-5.0.cfg
```

Paste:

```json
{
  "target": "linux/amd64",
  "http": "127.0.0.1:56741",
  "workdir": "/proj/ecs-251-PG0/$USER/syzkaller/workdir",
  "kernel_obj": "/proj/ecs-251-PG0/$USER",
  "image": "/proj/ecs-251-PG0/$USER/syzkaller/bullseye.img",
  "sshkey": "/proj/ecs-251-PG0/$USER/syzkaller/bullseye.id_rsa",
  "syzkaller": "/proj/ecs-251-PG0/$USER/syzkaller",
  "procs": 1,
  "type": "qemu",
  "vm": {
    "count": 2,
    "kernel": "/proj/ecs-251-PG0/$USER/linux-5.0-bzImage",
    "cpu": 2,
    "mem": 2048,
    "cmdline": "root=/dev/sda console=ttyS0 earlyprintk=serial net.ifnames=0 selinux=0 kaslr=off ignore_loglevel loglevel=7 panic_on_oops=0 init=/sbin/init",
    "qemu_args": "-machine accel=tcg"
  }
}
```

Create the workdir and fix permissions:

```bash
mkdir -p /proj/ecs-251-PG0/$USER/syzkaller/workdir

sudo chown -R $USER:ecs-251-PG0 /proj/ecs-251-PG0/$USER/syzkaller
sudo chown -R $USER:ecs-251-PG0 /proj/ecs-251-PG0/$USER/syzkaller/bullseye
```

---

## 5. Start Fuzzing

```bash
cd /proj/ecs-251-PG0/$USER/syzkaller
./bin/syz-manager -config=linux-5.0.cfg
```

The web UI is available at **http://127.0.0.1:56741** once the manager is running.

---

## 6. View Dashboard from Mac

```bash
ssh -L 56741:127.0.0.1:56741 $USER@<cloudlab-node>
```

Then open: http://127.0.0.1:56741

---

## 7. If VM Crashes

Before restarting, remove stale VM state:

```bash
rm -rf workdir/instance-*
./bin/syz-manager -config=linux-5.0.cfg
```

---

## Notes

- Login inside VM console: `root` with no password (press Enter)
- `exec total` increasing in the dashboard = fuzzing is working
