# PawnIO modules

Two signed modules from
[namazso/PawnIO.Modules](https://github.com/namazso/PawnIO.Modules),
release [0.2.12](https://github.com/namazso/PawnIO.Modules/releases/tag/0.2.12),
unmodified. They execute inside the PawnIO driver and expose byte reads
and writes on a fixed set of ports:

- `LpcACPIEC.bin`: the classic ACPI EC ports 0x62/0x66.
- `LpcThinkpadEC.bin`: the ThinkPad EC window at 0x1600-0x161f, the EC
  command interface plus the TWR mailbox. It only loads on machines
  that report a LENOVO ThinkPad and answer on 0x1604.

Both are LGPL-2.1, see `COPYING`; source in the repository above.

Deploy them next to `TPFanControl.exe`. The PawnIO driver itself is not
redistributed here; users install it from [pawnio.eu](https://pawnio.eu)
or `winget install namazso.PawnIO`.

sha256:

- `LpcACPIEC.bin`: `c38fd116e7aff4d1fdb0a494e296be0a6708e5a22fc72f14587442fb7f8f7906`
- `LpcThinkpadEC.bin`: `54bbf7f10d3511abfff167597485926ddf1587045f6ae849671cfaa85f97a1db`
