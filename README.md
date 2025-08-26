# `culvert` - A test and debug tool for BMC AHB bridges

## Warning

DO NOT USE THIS TOOL IF YOU HAVE NOT UNDERSTOOD ITS BEHAVIOURS IN THE CONTEXT
OF YOUR TARGET MACHINE.

This tool pokes at low-level hardware and firmware interfaces of your machine.
Particularly, the act of probing implies uncertainty about the consequences if
the target device is not the device we hoped to find.

Use of this tool may lead to instability, crashes or critical failures of the
target machine.

## Introduction

ASPEED BMCs have several implicit modes of operation:

1. Development: BMC / host firmware bring-up
2. IO-Expander: The BMC does not run a dedicated firmware
3. Provisioned: The BMC runs a dedicated firmware

To provide for 1 and 2, the BMC exposes features on several of its slave
interfaces that allow the host to reach into the BMC and perform arbitrary
operations in its physical address space.

The significant interfaces hang off of both the PCIe and LPC buses, as either
may not be connected to the host CPU in a given platform design.

Mode 3 is also typically used, but for the AST2400 and AST2500s the hardware is
configured for modes 1 and 2 at cold boot. Thus the onus is on BMC firmware
developers to ensure that all the interfaces are secured if they wish to
maintain confidentiality, integrity and availability of the BMC's at-rest data
and runtime environments.

## Interfaces

* PCIe VGA P2A: A PCIe MMIO interface providing a arbitrary AHB access via a
  64kiB sliding window

  * Write filters can be configured in the SCU to protect the integrity of
    coarse-grained AHB regions. By default the write filters are not enabled
    (all AHB regions are writable).

* iLPC2AHB: A SuperIO logical device providing arbitrary AHB access

  * A write filter that covers the entire AHB is exposed in the LPC controller.
    By default the write filter is not enabled (AHB is writable).

* Debug UART: A hardware-provided UART debug shell with arbitrary AHB access

  * Available on one of either UART1 or UART5

* X-DMA: Arbitrary M-Bus access

* PCIe BMC device: A collection of fixed PCIe MMIO interfaces providing
  restricted AHB access via 4kiB windows

* LPC2AHB: A BMC-controlled mapping of LPC FW cycles onto the AHB

## Tool Features

* Currently supports use of the P2A, iLPC2AHB, LPC2AHB and Debug UART interfaces

* Probes for the availability of all interfaces over any available interface

  * Can optionally set the exit status based on confidentiality and integrity
    requirements. Ideal for integration into platform security test suites.

* In-band BMC console from the host

* Reflash or dump the firmware of a running BMC from the host

  * The BMC CPU is clock-gated immediately prior to beginning the flash
    operations, and ungated immediately prior to a SoC reset subsequent to
    completion of the flash operations.

* Read and write BMC RAM

* Also supports the Linux `/dev/mem` interface for execution on the BMC itself

* [Expose internal JTAG master as OpenOCD-compatible bitbang interface](docs/OpenOCD.md)

  * Can access internal BMC/ARM CPU or externally attached JTAG devices
    (like CPLDs or even host CPUs)

## Building

The can be built for multiple architectures. It's known to run on the following:

* x86\_64
* powerpc64, powerpc64le
* armv6
* aarch64

It can be built with:

```
$ meson setup build && meson compile -C build
```

or to cross compile:
```
$ meson setup build-arm --cross-file meson/arm-linux-gnueabi-gcc.ini && meson compile -C build-arm
```

For arm64 (also known as `aarch64`) a different cross compile config is
required:
```
$ meson setup build-aarch64 --cross-file meson/aarch64-linux-gnu-gcc.ini && meson compile -C build-aarch64
```

#### Dependencies (Debian)
```
apt install build-essential flex swig bison meson device-tree-compiler libyaml-dev qemu-user
```

## Execution and Example output

```
$ ./build/src/culvert --help
Usage: culvert [OPTION...] <cmd> [CMD_OPTIONS]...

Culvert — A Test and Debug Tool for BMC AHB Interfaces

  -l, --list-bridges         List available bridge drivers
  -q, --quiet                Don't produce any output
  -s, --skip-bridge=BRIDGE   Skip BRIDGE driver
  -v, --verbose              Get verbose output
  -?, --help                 Give this help list
      --usage                Give a short usage message
  -V, --version              Print program version

Mandatory or optional arguments to long options are also mandatory or optional
for any corresponding short options.

Available commands:
  console              Start a getty on the BMC console
  coprocessor          Do things on the coprocessors of the AST2600
  debug                Read or write 4 bytes of data via the AHB bridge
  devmem               Read or write data to /dev/mem
  ilpc                 Read or write via iLPC
  jtag                 Start a JTAG OpenOCD Bitbang server
  otp                  Read and write the OTP configuration (AST2600-only)
  p2a                  Read or write data via p2a devices
  probe                Probe for any BMC
  read                 Read data from the FMC or RAM
  replace              Replace a portion in the memory
  reset                Reset a component of the BMC chip
  sfc                  Read, write or erase areas of a supported SFC
  trace                Trace what happens on a register
  write                Write data to the FMC or RAM
```

```
# ./culvert probe --require confidentiality
debug:  Permissive
        Debug UART port: UART5
xdma:   Permissive
        BMC: Disabled
        VGA: Enabled
        XDMA on VGA: Enabled
        XDMA is constrained: No
p2a:    Permissive
        BMC: Disabled
        VGA: Enabled
        MMIO on VGA: Enabled
        [0x00000000 - 0x0fffffff]   Firmware: Writable
        [0x10000000 - 0x1fffffff]     SoC IO: Writable
        [0x20000000 - 0x2fffffff]  BMC Flash: Writable
        [0x30000000 - 0x3fffffff] Host Flash: Writable
        [0x40000000 - 0x5fffffff]   Reserved: Writable
        [0x60000000 - 0x7fffffff]   LPC Host: Writable
        [0x80000000 - 0xffffffff]       DRAM: Writable
ilpc:   Disabled
# echo $?
1
#
```

```
# ./culvert probe --help
Usage: culvert probe [OPTION...]
            [-l] [-r REQUIREMENT] [via DRIVER [INTERFACE [IP PORT USERNAME
            PASSWORD]]]

Probe command

  -l, --list-interfaces      List available interfaces
  -r, --require=REQUIREMENT  Requirement to probe for
  -?, --help                 Give this help list
      --usage                Give a short usage message
  -V, --version              Print program version

Mandatory or optional arguments to long options are also mandatory or optional
for any corresponding short options.

Supported requirements:
  integrity        Require integrity
  confidentiality  Require confidentiality

Report bugs to GitHub amboar/culvert.

# ./culvert probe via debug-uart /dev/ttyUSB0
[*] Opening /dev/ttyUSB0
[*] Entering debug mode
xdma:   Restricted
        BMC: Disabled
        VGA: Enabled
        XDMA on VGA: Enabled
        XDMA is constrained: Yes
p2a:    Permissive
        BMC: Disabled
        VGA: Enabled
        MMIO on VGA: Enabled
        [0x00000000 - 0x0fffffff]   Firmware: Writable
        [0x10000000 - 0x1fffffff]     SoC IO: Writable
        [0x20000000 - 0x2fffffff]  BMC Flash: Writable
        [0x30000000 - 0x3fffffff] Host Flash: Writable
        [0x40000000 - 0x5fffffff]   Reserved: Writable
        [0x60000000 - 0x7fffffff]   LPC Host: Writable
        [0x80000000 - 0xffffffff]       DRAM: Writable
debug:  Permissive
        Debug UART port: UART1
debug:  Permissive
        Debug UART port: UART5
ilpc:   Disabled
[*] Exiting debug mode
```
