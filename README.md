# QEMU DANGER x86 README

QEMU DANGER x86 is a revision of QEMU. This allows you to ignore/revise virtual x86/64 CPU privileges!

For example, a Ring3 program can execute Ring0 instructions or read and write DATA!

[WindowsDanger]: https://github.com/UEFI-code/WindowsDanger
[LinuxDanger]: https://github.com/UEFI-code/linux-danger

We will research together with [WindowsDanger] and [LinuxDanger].

![screenshot](screenshot.png)

# Magic Instructions for Debugging

Capture output values at I/O ports "233" and "0x2333" and output "\a" or a prompt to the terminal.

## Stack Dump: E399h

```asm
mov dx, 2333h; Destination->Magic I/O Port
mov ax, E399h; Command->Stack Dump
mov bx, 16; Stack->Dump Size
out dx, ax; TRIGGER!!
```

## Just Alert

For example, execute the following in WinXP's CMD:

```shell
debug
-o e9 e9
```

Then execute:

```shell
mov dx, 2333h
mov ax, 2333h
out dx, ax
```

And "\a" will be output.

# QEMU

QEMU is a generic and open source machine & userspace emulator and
virtualizer.

QEMU is capable of emulating a complete machine in software without any
need for hardware virtualization support. By using dynamic translation,
it achieves very good performance. QEMU can also integrate with the Xen
and KVM hypervisors to provide emulated hardware while allowing the
hypervisor to manage the CPU. With hypervisor support, QEMU can achieve
near native performance for CPUs. When QEMU emulates CPUs directly it is
capable of running operating systems made for one machine (e.g. an ARMv7
board) on a different machine (e.g. an x86_64 PC board).

QEMU is also capable of providing userspace API virtualization for Linux
and BSD kernel interfaces. This allows binaries compiled against one
architecture ABI (e.g. the Linux PPC64 ABI) to be run on a host using a
different architecture ABI (e.g. the Linux x86_64 ABI). This does not
involve any hardware emulation, simply CPU and syscall emulation.

QEMU aims to fit into a variety of use cases. It can be invoked directly
by users wishing to have full control over its behaviour and settings.
It also aims to facilitate integration into higher level management
layers, by providing a stable command line interface and monitor API.
It is commonly invoked indirectly via the libvirt library when using
open source applications such as oVirt, OpenStack and virt-manager.

QEMU as a whole is released under the GNU General Public License,
version 2. For full licensing details, consult the LICENSE file.


# Documentation

Documentation can be found hosted online at
[https://www.qemu.org/documentation/]. The documentation for the
current development version that is available at
[https://www.qemu.org/docs/master/] is generated from the ``docs/``
folder in the source tree, and is built by [Sphinx](https://www.sphinx-doc.org/en/master/).


# Building

QEMU is multi-platform software intended to be buildable on all modern
Linux platforms, OS-X, Win32 (via the Mingw64 toolchain) and a variety
of other UNIX targets. The simple steps to build QEMU are:

if you are building on a Mac:

```shell
brew install pkg-config glib libtool automake autoconf pixman ninja sdl2
pip3 install meson
```

```shell
mkdir build
cd build
../configure
make -j8
```

Additional information can also be found online via the QEMU website:

* [Hosts/Linux](https://wiki.qemu.org/Hosts/Linux)
* [Hosts/Mac](https://wiki.qemu.org/Hosts/Mac)
* [Hosts/W32](https://wiki.qemu.org/Hosts/W32)