# TPFanCtrl2 2.5.1-fix.6 — proposed upstream contribution

This is an **unofficial** contribution based on the user-supplied `TPFanCtrl2-main.zip` from `mews-se/TPFanCtrl2`. It is not an upstream release.

## Goals

- Preserve the existing UI, configuration format, port backends, and Visual Studio project.
- Address failed EC temperature reads and resume handling.
- Allow building the existing main `TPFanControl.exe` with Code::Blocks 20.03 / MinGW-w64 x64.

## Source code changes

- `fancontrol/fanstuff.cpp`: validate usable temperature before Smart regulation; compare CPU values in paired samples; avoid treating a zero sensor value as an idle CPU; adjust level 64 spin-up threshold (4000 to 3200 RPM). The 3200 RPM policy adjustment is **separate from** the safety fix and should be reviewed independently by maintainers.
- `fancontrol/fancontrol.cpp` and `.h`: introduce delayed resume validation and restored mode state; avoid blocking Event Log callback with sleep; avoid truncating a pointer passed to the worker thread.
- `fancontrol/misc.cpp`: use a correct Windows thread start routine signature.
- `fancontrol/winstuff.cpp`, `.h`: pointer-width-safe Windows handle casts.
- `*/SystemTraySDK.cpp`: match `OnTrayNotification(WPARAM, LPARAM)` signature on x64.
- `fancontrol/_prec.h`, `fancontrol/mingw_compat.h`: MinGW compatibility declarations.
- `fancontrol/res/fancontrol.rc`, `fancontrol/fancontrol.h`: unofficial version identification.
- `TPFanCtrl2.cbp`: Code::Blocks 20.03 project for the *main* executable only. The original Visual Studio solution remains unchanged. The TPFCIcon helper binaries are **not** built by this project.

## Testing and limitations

- Reporter: Lenovo ThinkPad X395, Windows; PawnIO backend.
- User reported that the main executable compiled in Code::Blocks Debug, with 0 errors and 27 warnings, and started after `LpcACPIEC.bin` and `TPFanControl.ini` were placed alongside it.
- User reported a successful close-lid/resume test; no detailed logs or multi-device testing provided.
- Not verified with the maintainer's build pipeline, Visual Studio, other ThinkPad models, both-fan configurations, or a clean MinGW environment.
- `Debug` / `Release` helper `TPFCIcon` executables are not included. The main program and helper modules may need separate project targets upstream.
- Treat the resume/sensor changes as candidate fixes that require code review, especially for unusual EC behavior. This patch has not been formally safety-audited.

## How to review

See `changes.patch` in the parent submission ZIP. Apply it to the exact baseline shown in `BASELINE_SHA256.txt`, inspect the diff, and build under Code::Blocks. Maintainers may want to split the patch into (1) EC/suspend fixes, (2) MinGW portability and (3) Code::Blocks project.

No user-specific `TPFanControl.ini`, private paths, third-party driver binaries, or compiled executables are added by this patch. Existing vendor binaries present in the upstream repository remain unchanged.

Public-domain contribution: the upstream repository's terms for contributions apply. The person opening the PR should review and accept those terms themselves.
