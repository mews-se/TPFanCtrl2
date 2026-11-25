// --------------------------------------------------------------
//
//  Thinkpad Fan Control
//
// --------------------------------------------------------------
//
//	This program and source code is in the public domain.
//
// --------------------------------------------------------------
//
//  State shared between the instance that owns the EC (the engine)
//  and the user interface clients attached to it.
//
// --------------------------------------------------------------

#ifndef SHAREDSTATE_H
#define SHAREDSTATE_H

#pragma once

// Each packed diagnostic word contains the raw 16-bit sample and its
// status in bits 16..23. A valid range is not a physical RPM guarantee.
enum TachSampleStatus {
	TACH_UNKNOWN = 0,
	TACH_STALE = 1,
	TACH_INVALID = 2,
	TACH_FRESH_UNVERIFIED = 3,
	TACH_FRESH_OBF = 4,
};

inline LONG PackTachSample(unsigned int rpm, TachSampleStatus status) {
	return static_cast<LONG>((static_cast<unsigned int>(status) << 16) | (rpm & 0xffff));
}

struct FCSHARED {
	volatile LONG stateSeq;   // bumped by the engine after every publish
	volatile LONG cmdSeq;     // bumped by a client after every command
	volatile LONG ackSeq;     // cmdSeq the engine has already acted on

	int mode;                 // 1 bios, 2 smart, 3 manual
	int smartLevel;           // active smart profile, 0 or 1
	int fanCtrl;              // raw contents of EC register 0x2f
	int fan1lo, fan1hi;
	int fan2lo, fan2hi;
	int sensors[12];

	int cmdMode;              // mode the user picked
	int cmdSmart;             // smart profile the user picked, -1 for none
	char cmdLevelText[16];    // manual level as typed, parsed by the engine

	volatile LONG traceSeq;   // bumped by the engine for every line below
	char traceLines[48][160]; // ring of engine log lines, mirrored by clients

	// Optional version-1 suffix. Preserve all existing offsets.
	LONG tachVersion;
	volatile LONG tachSample1;
	volatile LONG tachSample2;
};

bool SharedState_Create();   // engine side, fails if it cannot be created
bool SharedState_Attach();   // client side, fails if no engine published one
void SharedState_Close();

// a section dies with its last handle, so a killed engine leaves nothing stale
bool SharedState_EngineRunning();

FCSHARED* SharedState();     // NULL until created or attached

// Engine-only diagnostics; do not modify cached RPM or control policy.
void SharedState_PublishTach(unsigned char lo1, unsigned char hi1,
	unsigned char lo2, unsigned char hi2, bool strictObf);
void SharedState_MarkTachStale();

#endif // SHAREDSTATE_H
