// --------------------------------------------------------------
//
//  Thinkpad Fan Control
//
// --------------------------------------------------------------
//
//	This program and source code is in the public domain.
//
// --------------------------------------------------------------

#include "_prec.h"
#include "sharedstate.h"
#include "winstuff.h"

static const char* SHARED_NAME = "Global\\TPFanControl_State";

static HANDLE s_map = NULL;
static FCSHARED* s_view = NULL;

FCSHARED* SharedState() {
	return s_view;
}

bool SharedState_Create() {
	SECURITY_ATTRIBUTES sa;
	bool shared = SharedSecurity(&sa);

	s_map = ::CreateFileMappingA(INVALID_HANDLE_VALUE, shared ? &sa : NULL,
		PAGE_READWRITE, 0, sizeof(FCSHARED), SHARED_NAME);

	if (shared)
		::LocalFree(sa.lpSecurityDescriptor);

	if (!s_map)
		return false;

	s_view = (FCSHARED*)::MapViewOfFile(s_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(FCSHARED));
	if (!s_view) {
		::CloseHandle(s_map);
		s_map = NULL;
		return false;
	}

	::ZeroMemory(s_view, sizeof(FCSHARED));
	s_view->cmdSmart = -1;

	return true;
}

bool SharedState_Attach() {
	s_map = ::OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, SHARED_NAME);
	if (!s_map)
		return false;

	s_view = (FCSHARED*)::MapViewOfFile(s_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(FCSHARED));
	if (!s_view) {
		::CloseHandle(s_map);
		s_map = NULL;
		return false;
	}

	return true;
}

bool SharedState_EngineRunning() {
	HANDLE h = ::OpenFileMappingA(FILE_MAP_READ, FALSE, SHARED_NAME);
	if (!h)
		return false;

	::CloseHandle(h);

	return true;
}

void SharedState_Close() {
	if (s_view) {
		::UnmapViewOfFile(s_view);
		s_view = NULL;
	}

	if (s_map) {
		::CloseHandle(s_map);
		s_map = NULL;
	}
}
