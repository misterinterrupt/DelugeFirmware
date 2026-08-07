# Deluge Community Firmware

## Dear Deluge owners
If you want to start using the community firmware please visit https://synthstromaudible.github.io/DelugeFirmware/ for all relevant details.

## About
The [Deluge](https://synthstrom.com/product/deluge/) from [Synthstrom Audible](https://synthstrom.com/) is a portable sequencer, synthesizer and sampler. Synthstrom Audible decided to [open source](https://synthstrom.com/open/) the firmware application that runs on the Deluge to allow the community to customize and extend the capabilities of the device. 

The hardware is built around a Renesas RZ/A1L processor with an Arm® Cortex®-A9 core running at 400MHz and 3MB of on-chip SRAM. Connected to that is a 64MB SDRAM chip, a PIC for button handling and either a 7-Segment array or an OLED screen. The application is written in C and C++ and some occasional assembly in bare metal style without an operating system.

## Important links
* For acquiring and using the firmware visit the [community firmware website](https://synthstromaudible.github.io/DelugeFirmware)
* Once you have the firmware, consult our detailed [update guide](https://github.com/SynthstromAudible/DelugeFirmware/wiki/Update-guide) for instructions on how to install it
* To contribute to the project with code, bug reports, suggestions, and feedback see: [How to contribute to Deluge Firmware](docs/CONTRIBUTING.md)
* To learn about the toolchain, application, and to start working on the code continue with: [Getting Started with Deluge Development](docs/dev/getting_started.md)
* Please also visit the [Synthstrom Forum](https://forums.synthstrom.com/) and the community [Discord](https://discord.gg/BnRcyFSgaT) to interact with other users and the developers
* You can donate to the project using the [Synthstrom Patreon](https://www.patreon.com/Synthstrom)

## J-Link RAM Flashing & Debugging (Linux)

### 1. Direct Command Line Flashing (Non-persistent RAM load)
To load and execute `deluge.elf` directly on the target (Renesas RZ/A1L) using `JLinkExe` over SWD:

Create a command script (e.g. `scripts/.flash_cmd.jlink`):
```text
loadfile build/Release/deluge.elf
r
g
exit
```

Then run `JLinkExe`:
```bash
JLinkExe -device R7S721020 -if SWD -speed 4000 -autoconnect 1 \
  -exitonerror 1 -CommandFile scripts/.flash_cmd.jlink < /dev/null
```
> **Important:** Redirecting standard input (`< /dev/null`) is essential on Linux host systems. Without redirecting stdin, interactive terminal prompts disrupt the `-autoconnect 1` sequence.

### 2. GDB Debugging & Binary Naming Note (`dbt debug -j`)
When using `dbt debug -j` on Linux with standard SEGGER installs, `dbt` expects `JLinkGDBServerCL` (or `JLinkGDBServerCLExe`). On some Linux systems (such as SEGGER's standard Linux package install), the command is installed as `JLinkGDBServerCLExe`. 

Ensure `JLinkGDBServerCLExe` (or a symlink to it) is in your `PATH`, or pass the executable explicitly using the `-c` flag:
```bash
./dbt debug -j -c JLinkGDBServerCLExe
```
Then connect GDB via `./dbt gdb` or standard `arm-none-eabi-gdb`.

