# Linux 5.15 + Syzkaller Setup Guide

**CloudLab Version**

- Host OS: Ubuntu 22.04
- Target: Linux 5.15 with KCOV + KASAN
- VM: QEMU (KVM)

---

```bash
sudo apt update
sudo apt install docker.io qemu-system-x86 debootstrap git
sudo usermod -aG docker $USER
```

Log out and log back in
---

## 2. Build Linux 5.15 in Docker

Start an Ubuntu 20.04 Docker container:

```bash
docker run -it ubuntu:20.04 /bin/bash
```

Inside the container, install build dependencies and clone the kernel:

```bash
apt update
apt install -y build-essential bc flex bison \
  libssl-dev libelf-dev libncurses-dev git

cd /root
git clone --branch v5.15 \
  https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git linux-5.15
cd linux-5.15

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
  --enable BPF_SYSCALL \
  --enable CGROUP_BPF \
  --enable JFS_FS \
  --enable XFS_FS \
  --enable FAULT_INJECTION \
  --enable FAULT_INJECTION_DEBUG_FS \
  --enable FAILSLAB \
  --disable RANDOMIZE_BASE

make olddefconfig
make -j$(nproc)
```

---

## 3. Copy Kernel Files to Host

From a second terminal on the host, find the container ID:

```bash
docker ps
```

Copy the kernel files out:

```bash
docker cp <container_id>:/root/linux-5.15/arch/x86/boot/bzImage \
  /proj/ecs-251-PG0/kkringel/linux-5.15-bzImage

docker cp <container_id>:/root/linux-5.15/vmlinux \
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
```

### Create Debian VM Image

```bash
./tools/create-image.sh -d bullseye
```

### Configure Syzkaller

Create the config file at `/proj/ecs-251-PG0/kkringel/syzkaller/linux-5.15.cfg`:

```json
{
  "target": "linux/amd64",
  "http": "127.0.0.1:56741",
  "workdir": "/proj/ecs-251-PG0/<user/syzkaller/workdir",
  "kernel_obj": "/proj/ecs-251-PG0/<user>",
  "image": "/proj/ecs-251-PG0/<user>/syzkaller/bullseye.img",
  "sshkey": "/proj/ecs-251-PG0/<user>/syzkaller/bullseye.id_rsa",
  "syzkaller": "/proj/ecs-251-PG0/<user>/syzkaller",
  "procs": 4,
  "type": "qemu",
  "vm": {
    "count": 4,
    "kernel": "/proj/ecs-251-PG0/<user>/linux-5.15-bzImage",
    "cpu": 2,
    "mem": 2048,
    "cmdline": "root=/dev/sda console=ttyS0 earlyprintk=serial net.ifnames=0 panic=1",
    "qemu_args": "-machine accel=kvm"
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

## 5. Start the VM Manually (for Crash Reproduction)

```bash
sudo usermod -aG kvm $USER
newgrp kvm

qemu-system-x86_64 \
  -m 2048 \
  -smp 2 \
  -kernel /proj/ecs-251-PG0/kkringel/linux-5.15-bzImage \
  -drive file=/proj/ecs-251-PG0/kkringel/syzkaller/bullseye.img,format=raw \
  -append "root=/dev/sda console=ttyS0 earlyprintk=serial net.ifnames=0 panic=1" \
  -netdev user,id=net0,hostfwd=tcp::10022-:22 \
  -device e1000,netdev=net0 \
  -machine accel=kvm \
  -nographic
```

Wait for the `syzkaller login:` prompt, then log in as `root`.

---

## 6. SSH Permission Fixes (Required After Each Boot)

These changes are lost on reboot and must be reapplied each time.

### Enable root login and public key auth

```bash
sed -i 's/#PermitRootLogin.*/PermitRootLogin yes/' /etc/ssh/sshd_config
sed -i 's/#PubkeyAuthentication.*/PubkeyAuthentication yes/' /etc/ssh/sshd_config
sed -i 's/#PasswordAuthentication yes/PasswordAuthentication yes/' /etc/ssh/sshd_config
```

### Set a root password

```bash
passwd root
# Enter password when prompted (e.g. 'root')
```

### Add SSH public key

On the host, get the public key:

```bash
cat /proj/ecs-251-PG0/kkringel/syzkaller/bullseye.id_rsa.pub
```

Inside the VM, paste it:

```bash
mkdir -p /root/.ssh
echo "PASTE_PUBLIC_KEY_HERE" > /root/.ssh/authorized_keys
chmod 700 /root/.ssh
chmod 600 /root/.ssh/authorized_keys
```

### Restart SSH

```bash
service ssh restart
```

---

## Reproducing a Crash

Copy the required files from the host to the VM:

```bash
scp -P 10022 \
  -i /proj/ecs-251-PG0/kkringel/syzkaller/bullseye.id_rsa \
  -o StrictHostKeyChecking=no \
  /proj/ecs-251-PG0/kkringel/syzkaller/bin/linux_amd64/syz-execprog \
  /proj/ecs-251-PG0/kkringel/syzkaller/bin/linux_amd64/syz-executor \
  /proj/ecs-251-PG0/kkringel/crash_info/repro.syz \
  root@localhost:/root/
```

Inside the VM, run the reproducer:

```bash
cd /root
chmod +x syz-execprog syz-executor
./syz-execprog -executor ./syz-executor repro.syz
dmesg > /root/dmesg.log
```

Copy the dmesg log back to the host:

```bash
scp -P 10022 \
  -i /proj/ecs-251-PG0/kkringel/syzkaller/bullseye.id_rsa \
  -o StrictHostKeyChecking=no \
  root@localhost:/root/dmesg.log \
  /proj/ecs-251-PG0/kkringel/crash_info/dmesg.log
```

---

## Running Syzkaller To Find Bugs

```bash
mkdir -p /proj/ecs-251-PG0/kkringel/syzkaller/workdir
cd /proj/ecs-251-PG0/kkringel/syzkaller
./bin/syz-manager -config=linux-5.15.cfg 2>&1 | tee workdir/manager.log
```

The web UI is available at **http://127.0.0.1:56741** once the manager is running. Crashes found during fuzzing appear there automatically.

