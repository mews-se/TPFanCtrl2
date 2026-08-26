# PawnIO module

`LpcACPIEC.bin` is the signed ACPI EC module from
[namazso/PawnIO.Modules](https://github.com/namazso/PawnIO.Modules),
release [0.2.10](https://github.com/namazso/PawnIO.Modules/releases/tag/0.2.10),
unmodified. It executes inside the PawnIO driver and exposes byte reads
and writes on the classic ACPI EC ports 0x62/0x66 only. The module is
LGPL-2.1 — see `COPYING`; source in the repository above.

Deploy it next to `TPFanControl.exe`. The PawnIO driver itself is not
redistributed here; users install it from [pawnio.eu](https://pawnio.eu)
or `winget install namazso.PawnIO`.

sha256: `c38fd116e7aff4d1fdb0a494e296be0a6708e5a22fc72f14587442fb7f8f7906`
