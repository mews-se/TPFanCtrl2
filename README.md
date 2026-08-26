# TPFanCtrl2

Fan control for ThinkPads running Windows 10/11.

This repository carries on FanDjango's TPFanCtrl2 line, which was archived
in August 2026. The full history is preserved here — every branch and tag,
and the release binaries from V2.3.4 through V2.3.23 mirrored under
[Releases](../../releases).

## Lineage

troubadix's TPFanControl (thinkwiki.de) → [ThinkPad-Forum/TPFanControl](https://github.com/ThinkPad-Forum/TPFanControl)
→ [byrnes' dual-fan mod](https://github.com/byrnes/TPFanControl)
→ [Shuzhengz/TPFanCtrl2](https://github.com/Shuzhengz/TPFanCtrl2)
→ [FanDjango/TPFanCtrl2](https://github.com/FanDjango/TPFanCtrl2) (archived)
→ this repository. Public domain all the way through.

## How it works

One executable, two roles:

- The **engine** owns the embedded controller and drives the fan from a
  configurable temperature curve. It runs either as a plain window or as
  the `TPFanControl` Windows service, which controls the fan from boot as
  SYSTEM with no window.
- The **tray window** is a client of the engine. With the service running
  it needs no rights at all: it draws the state the engine publishes,
  mirrors the engine's log and hands over mode changes.

## Changes since FanDjango V2.3.23

- Fixed a crash that killed a tray client on its first data cycle
  (uninitialized sensor name pointers in the client path).
- The engine log is mirrored into client windows, timestamps intact,
  including a backlog of recent lines when the window starts.
- The tray menu no longer competes with the engine for the EC mutex;
  the hardware toggles are grayed out in a client.
- `ErraticSensorGuard` quarantines sensors that swing tens of degrees
  back and forth between cycles — multiplexed EC registers on newer
  machines that hold no temperature.
- `LidSmartLevel` runs a second smart profile while the lid is closed,
  for machines that work docked.
- The EC is reached through [PawnIO](https://pawnio.eu), so fan control
  works with memory integrity (HVCI) enabled. TVicPort remains as a
  fallback, loaded dynamically only when needed.

## Requirements

The EC is reached through one of two port drivers, tried in this order
(override with `PortBackend=` in the ini):

- **PawnIO** — install it from [pawnio.eu](https://pawnio.eu) or
  `winget install namazso.PawnIO`, and keep `LpcACPIEC.bin` (bundled
  here under `fancontrol/pawnio/`, part of the release zip) next to the
  exe. The driver is WHQL-signed and works with memory integrity (HVCI)
  enabled — this is the right choice on any current Windows 11 machine,
  where TVicPort's driver is refused. The signed module reaches the EC
  through the classic ports 0x62/0x66.
- **TVicPort** — put `TVicPort.dll` next to the exe with its kernel
  driver (`TVicPort64.sys` in `System32\drivers`) in place. Both ship
  with the original TPFanControl installer from the SourceForge days;
  uninstalling that program removes them again, so keep your own
  copies. Only works with memory integrity off, and it is the only
  backend that reaches the `UseTWR` sensor interface.

## Install

1. Put `TPFanControl.exe`, `TPFanControl.ini` and `LpcACPIEC.bin` (or
   `TVicPort.dll`) in a folder of their own, e.g.
   `C:\Program Files\TPFanCtrl2`.
2. Run the exe as administrator once and enable **Start with Windows** in
   the tray menu. That installs the service and a run entry that brings
   up the tray window at logon, without elevation prompts. The same from
   a prompt: `TPFanControl.exe -i` as administrator.
3. Adjust `TPFanControl.ini` next to the exe. The ones that matter most:
   - `Level=temp fan hystUp hystDown` — the fan curve, one line per step
   - `IgnoreSensors=` — sensors that should not drive the fan
   - `SingleFan=1` — on machines with one fan
   - `PowerSuspendMode=2` — keep controlling the fan with the lid closed,
     for docked use; the default hands the fan to the BIOS on lid close

To uninstall, disable **Start with Windows** in the menu (or run
`TPFanControl.exe -u` as administrator) and delete the folder.

## Building

Visual Studio Build Tools with the C++ workload, toolset v145. From a
developer prompt:

```
msbuild fancontrol\fancontrol.sln /p:Configuration=Release /p:Platform=Win32
```

The icon helper projects under `TPFCIcon` and `TPFCIcon_noballons` still
name the v143 toolset; add `/p:PlatformToolset=v145` when building them
with current tools.

## Tested hardware

ThinkPad X13 Gen 3 (21BN, single fan) runs the service + tray setup as
its daily driver. For models confirmed on earlier versions, see the
[upstream README](https://github.com/Shuzhengz/TPFanCtrl2#readme).

## License

Public domain (Unlicense), inherited from upstream. No warranty — this
program writes to your embedded controller, use it at your own risk.
