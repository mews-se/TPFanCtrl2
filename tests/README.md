# Offline tests for the three EC changes

Run with Python 3 and g++ (C++17), without EC hardware:
- python tests/check_fan_commands.py
- python tests/check_transport.py
- python tests/check_tach_status.py

The first commit checks selector and command-confirmation behavior using a fake EC.
The second adds checked PawnIO operations and optional OBF wait (StrictEcReadObf=0
by default). The TVicPort API cannot report per-operation errors.
The third appends an optional IPC diagnostic suffix. Existing offsets are unchanged.
A new reader must map the extended size successfully and check tachVersion == 1.
Each atomic LONG contains the raw tach word in bits 0..15 and status in bits
16..23: 0 unknown, 1 stale, 2 outside the existing 0x1fff display limit,
3 new successful sample without OBF validation, 4 new successful sample with
OBF wait enabled. Fresh means the last successful polling result, not physical
truth or a guaranteed simultaneous snapshot of both fans. Failed polls mark
the diagnostic words stale without changing the prior raw value or legacy
stateSeq; readers must inspect the diagnostic words even if stateSeq is unchanged.
A single-fan system retains the existing duplicated fan2 value. The status is
diagnostic only; it does not override thermal protection or change the UI.
No real laptop operation is performed by these tests.

The fourth commit handles WAIT_ABANDONED as acquired, logs recovery for EC
callers and balances ownership. Run python tests/check_mutex.py for seven
deterministic ownership scenarios including worker exit, dialog-to-worker
handoff, timeout, failed wait and recursive close. This is a fake WinAPI test,
not an actual Windows threading or hardware test. Controller readiness
is still checked by the existing EC transaction path.
