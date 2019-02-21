## Current Capabilities and Restrictions

The implementation is currently restricted to running as `root` on the BMC and
will only evaluate the BMC's configuration with respect to the bridges
described in CVE-2019-6260. It does not demonstrate exploitation of the
bridges.

# A Test and Debug Tool for ASPEED BMC AHB Interfaces

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

* Probes for the availability of all interfaces over any available interface

* Currently supports just the BMC-side `/dev/mem` backend (no host-side
  support).

## Compiling

The tool compiles for multiple architectures, and at least the following have
been tested:

* x86_64
* powerpc64, powerpc64le
* armv6

Building is a matter of issuing `make`, while cross compilation is performed by
setting `CROSS_COMPILE` in the make environment, e.g:

```
$ make CROSS_COMPILE=powerpc64le-linux-gnu-
```

## Execution and Example output

```
root@bmc:/tmp# ./doit
[*] Not enough arguments
./doit: v0.2-14-g204ed4b608c8
Usage:

./doit probe
./doit devmem read ADDRESS
./doit devmem write ADDRESS VALUE
root@bmc:/tmp# ./doit probe
[*] Probing AHB interfaces
[*] Initialised devmem AHB interface
[*] Performing interface discovery via devmem
[*] Detected AST2500 A2
SuperIO: Disabled
VGA PCIe device: Enabled
MMIO on VGA device: Enabled
P2A write filter state:
0x00000000-0x0fffffff (Firmware): Read-write
0x10000000-0x1fffffff (SoC IO): Read-write
0x20000000-0x3fffffff (Flashes): Read-write
0x40000000-0x5fffffff (Reserved): Read-write
0x60000000-0x7fffffff (LPC Host): Read-write
0x80000000-0xffffffff (DRAM): Read-write
X-DMA on VGA device: Enabled
BMC PCIe device: Disabled
X-DMA is unconstrained: Yes
Debug UART: Disabled
```
