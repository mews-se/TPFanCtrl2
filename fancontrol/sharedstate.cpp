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
#include <cstddef>

static const char* SHARED_NAME = "Global\\TPFanControl_State";

static HANDLE s_map = NULL;
static FCSHARED* s_view = NULL;
static bool s_hasTachFields = false;

void SharedState_PublishTach(unsigned char lo1, unsigned char hi1,
	unsigned char lo2, unsigned char hi2, bool strictObf) {
	if (!s_view || !s_hasTachFields) return;
	const unsigned int rpm1 = (static_cast<unsigned int>(hi1) << 8) | lo1;
	const unsigned int rpm2 = (static_cast<unsigned int>(hi2) << 8) | lo2;
	const TachSampleStatus status = strictObf ? TACH_FRESH_OBF : TACH_FRESH_UNVERIFIED;
	::InterlockedExchange(&s_view->tachSample1, PackTachSample(rpm1, rpm1 > 0x1fff ? TACH_INVALID : status));
	::InterlockedExchange(&s_view->tachSample2, PackTachSample(rpm2, rpm2 > 0x1fff ? TACH_INVALID : status));
}

void SharedState_MarkTachStale() {
	if (!s_view || !s_hasTachFields) return;
	::InterlockedExchange(&s_view->tachSample1, PackTachSample(s_view->tachSample1, TACH_STALE));
	::InterlockedExchange(&s_view->tachSample2, PackTachSample(s_view->tachSample2, TACH_STALE));
}

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
	s_hasTachFields = true;
	s_view->tachVersion = 1;

	return true;
}

bool SharedState_Attach() {
	s_map = ::OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, SHARED_NAME);
	if (!s_map)
		return false;

	s_view = (FCSHARED*)::MapViewOfFile(s_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(FCSHARED));
	s_hasTachFields = s_view != NULL;
	if (!s_view)
		s_view = (FCSHARED*)::MapViewOfFile(s_map, FILE_MAP_ALL_ACCESS, 0, 0, offsetof(FCSHARED, tachVersion));
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
	s_hasTachFields = false;
	if (s_view) {
		::UnmapViewOfFile(s_view);
		s_view = NULL;
	}

	if (s_map) {
		::CloseHandle(s_map);
		s_map = NULL;
	}
}
