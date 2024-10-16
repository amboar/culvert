# OpenOCD software/virtual JTAG interface

> [!WARNING]
> Debugging/halting the BMC CPU core of the machine you're running `culvert` on
> might crash your host system & BMC at the same time and leave it in a state
> which is only recoverable via an external power cycle.

## Introduction

The ASPEED BMC chips contain a JTAG master peripheral, which is typically used
to program external components like CPLDs on a server mainboard from the BMC firmware.

As a special operating mode, the JTAG peripheral can also be reconfigured to
internally connect to the internal ARM CPU core of the BMC.
Through the various external interfaces (Debug UART, PCIe2AHB) this peripheral
can be used even when the ARM CPU itself isn't executing code.

This can be used to debug low-level BMC firmware issues even in scenarios where
the external JTAG I/O pins aren't accessible (like on most commercial server boards).

## Internals

culvert will setup the JTAG peripheral in such a way that the TCK, TMS, TDI/TDO lines
inside the ASPEED chip itself can be controlled through software.
This maps very well onto OpenOCDs `remote-bitbang` protocol, which sends commands to manipulate the I/O lines via TCP.
culvert will accept these commands and write the appropriate registers in the ASPEEDs JTAG peripheral.

The output of the JTAG peripheral can be re-routed to either:
- `arm` target - the internal ARM11 core inside the BMC
- `pcie` target - the internal PCIe PHY
- `external` target - the external JTAG pins (typically used for CPLDs or Arm64 host CPUs)

## Requirements

- OpenOCD (compiled with `remote-bitbang` feature enabled)
```bash
./bootstrap
./configure --enable-remote-bitbang
make
```
- GDB multiarch
- culvert

## ARM debugging how-to

Create a OpenOCD config file `ast2500.cfg`, containing connection and CPU details:
```
adapter driver remote_bitbang
remote_bitbang port 33333
remote_bitbang host localhost

transport select jtag
reset_config none

set _CHIPNAME ast2500

jtag newtap auto0 tap -irlen 5 -expected-id 0x07b76f0f

set _TARGETNAME $_CHIPNAME.cpu
target create $_TARGETNAME arm11 -chain-position auto0.tap
```

Run culvert (via Debug UART on `/dev/ttyUSB0` in this case):
```bash
culvert jtag /dev/ttyUSB0
```

> Wait for culvert to be ready:
> ```
> [*] Opening /dev/ttyUSB0
> [*] Entering debug mode
> [*] Ready to accept OpenOCD remote_bitbang connections on 127.0.0.1:33333
> ```

Run OpenOCD:
```bash
openocd -f ~/ast2500.cfg
```

> Wait for:
> ```
> Info: found ARM1176
> Info: ast2500.cpu: hardware has 6 breakpoints, 2 watchpoints
> Info: [ast2500.cpu] Examination succeed
> Info: [ast2500.cpu] starting gdb server on 3333
> Info : Listening on port 3333 for gdb connections
> ```

After OpenOCD has detected & examined the ARM11 CPU core, you can connect to it using GDB:
```bash
gdb-multiarch
```

```
set remotetimeout 50000 # only required when debugging via UART
set architecture armv6
target extended-remote localhost:3333
```

Using GDB over a Debug UART connection is painfully slow, but it'll work in a pinch.  
You can now use GDB to open the ELF binary of your Linux kernel, u-boot, etc. and try to debug it.

## External devices

Run culvert (via Debug UART on `/dev/ttyUSB0`) using the --target flag:
```bash
culvert jtag --target external /dev/ttyUSB0
```
