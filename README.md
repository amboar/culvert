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

## Building

The can be built for multiple architectures. It's known to run on the following:

* x86\_64
* powerpc64, powerpc64le
* armv6

It can be built with:

```
$ meson setup build && meson compile -C build
```

or to cross compile:
```
$ meson setup build-arm --cross-file meson/arm-linux-gnueabi-gcc.ini && meson compile -C build-arm
```

## Execution and Example output

```
$ ./build/src/culvert -h
culvert: v0.4.0-10-gb30b8364b75a
Usage:

culvert probe [INTERFACE [IP PORT USERNAME PASSWORD]]
culvert ilpc read ADDRESS
culvert ilpc write ADDRESS VALUE
culvert p2a vga read ADDRESS
culvert p2a vga write ADDRESS VALUE
culvert debug read ADDRESS INTERFACE [IP PORT USERNAME PASSWORD]
culvert debug write ADDRESS VALUE INTERFACE [IP PORT USERNAME PASSWORD]
culvert devmem read ADDRESS
culvert devmem write ADDRESS VALUE
culvert console HOST_UART BMC_UART BAUD USER PASSWORD
culvert read firmware [INTERFACE [IP PORT USERNAME PASSWORD]]
culvert read ram [INTERFACE [IP PORT USERNAME PASSWORD]]
culvert write firmware [INTERFACE [IP PORT USERNAME PASSWORD]]
culvert replace ram MATCH REPLACE
culvert reset TYPE WDT [INTERFACE [IP PORT USERNAME PASSWORD]]
culvert sfc fmc read ADDRESS LENGTH [INTERFACE [IP PORT USERNAME PASSWORD]]
culvert sfc fmc erase ADDRESS LENGTH [INTERFACE [IP PORT USERNAME PASSWORD]]
culvert sfc fmc write ADDRESS LENGTH [INTERFACE [IP PORT USERNAME PASSWORD]]
culvert otp read conf [INTERFACE [IP PORT USERNAME PASSWORD]]
culvert otp read strap [INTERFACE [IP PORT USERNAME PASSWORD]]
culvert otp write strap BIT VALUE [INTERFACE [IP PORT USERNAME PASSWORD]]
culvert otp write conf WORD BIT [INTERFACE [IP PORT USERNAME PASSWORD]]
culvert trace ADDRESS WIDTH MODE [INTERFACE [IP PORT USERNAME PASSWORD]]
```

```
$ ./culvert probe --require confidentiality digi,portserver-ts-16 <IP> <PORT> <USER> <PASSWORD>
Connecting to Digi Portserver TS 16 at <IP>:23
Logging into Digi Portserver TS
Configuring binary mode on port 4
Resetting port 4
Connecting to BMC console at <IP>:2104
Entering debug mode
Initialised Debug UART AHB interface
Performing interface discovery via Debug UART
Exiting debug mode
Have SuperIO: Yes
iLPC2AHB Bridge: Read-only
Have VGA PCIe device: Yes
Have MMIO on VGA device: Yes
P2A write filter state:
0x00000000-0x0fffffff (Firmware): Read-write
0x10000000-0x1fffffff (SoC IO): Read-write
0x20000000-0x3fffffff (Flashes): Read-write
0x40000000-0x5fffffff (Reserved): Read-write
0x60000000-0x7fffffff (LPC Host): Read-write
0x80000000-0xffffffff (DRAM): Read-write
Have X-DMA on VGA device: Yes
Have BMC PCIe device: No
X-DMA is unconstrained: No
Have debug UART: Yes
Debug UART enabled on: UART5
$ echo $?
1
$
```
