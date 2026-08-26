// --------------------------------------------------------------
//
//  Thinkpad Fan Control
//
// --------------------------------------------------------------
//
//	This program and source code is in the public domain.
//
// --------------------------------------------------------------

#ifndef __portaccess_H
#define __portaccess_H

// Backend-agnostic byte-wide port io towards the embedded controller.
//
// PawnIO (https://pawnio.eu) runs port io inside a signed sandboxed module,
// so it loads with memory integrity (HVCI) enabled, but the signed LpcACPIEC
// module only reaches the classic ACPI EC ports 0x62/0x66.  TVicPort reaches
// any port but its driver is refused by HVCI, so it remains as the fallback
// for systems without memory integrity.  Both are loaded dynamically; neither
// dll is required for the program itself to start.

enum PORTACCESS_BACKEND {
	PORTACCESS_NONE = 0,
	PORTACCESS_PAWNIO,
	PORTACCESS_TVICPORT,
};

bool PortAccess_Open(void);
void PortAccess_Close(void);

PORTACCESS_BACKEND PortAccess_Backend(void);
const char* PortAccess_BackendName(void);

// false when the active backend cannot reach the port at all
bool PortAccess_AllowsPort(USHORT port);

UCHAR PortAccess_ReadByte(USHORT port);
void PortAccess_WriteByte(USHORT port, UCHAR value);

// the backend opens before the engine can trace, so messages queue up here
int PortAccess_InitMessageCount(void);
const char* PortAccess_InitMessage(int index);

#endif
