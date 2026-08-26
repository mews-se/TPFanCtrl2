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
#include <winioctl.h>
#include "portaccess.h"

// PawnIO's device io control interface, from pawnio_um.h (the special
// exception in PawnIO's license covers independent programs speaking it).
// The input of an execute call is a 32 byte zero padded function name
// followed by the arguments as 64 bit cells; buffered io keeps this layout
// identical from a 32 bit process.
constexpr ULONG PAWNIO_DEVICE_TYPE = 41394;
constexpr ULONG PAWNIO_IOCTL_LOAD_BINARY = CTL_CODE(PAWNIO_DEVICE_TYPE, 0x821, METHOD_BUFFERED, FILE_ANY_ACCESS);
constexpr ULONG PAWNIO_IOCTL_EXECUTE_FN = CTL_CODE(PAWNIO_DEVICE_TYPE, 0x841, METHOD_BUFFERED, FILE_ANY_ACCESS);
constexpr ULONG PAWNIO_IOCTL_VERSION = CTL_CODE(PAWNIO_DEVICE_TYPE, 0x861, METHOD_BUFFERED, FILE_ANY_ACCESS);

constexpr auto PAWNIO_FN_NAME_LENGTH = 32u;
constexpr auto PAWNIO_MODULE_FILE = "LpcACPIEC.bin";
constexpr auto PAWNIO_MAX_MODULE_SIZE = 1024u * 1024u;

// what the signed LpcACPIEC module's port allowlist accepts
constexpr USHORT PAWNIO_EC_DATAPORT = 0x62;
constexpr USHORT PAWNIO_EC_CTRLPORT = 0x66;

// TVicPort.dll exports undecorated stdcall names
typedef BOOL(__stdcall* TVIC_OPEN)(void);
typedef void(__stdcall* TVIC_CLOSE)(void);
typedef UCHAR(__stdcall* TVIC_READPORT)(USHORT);
typedef void(__stdcall* TVIC_WRITEPORT)(USHORT, UCHAR);

static PORTACCESS_BACKEND g_backend = PORTACCESS_NONE;

static HANDLE g_pawnio = INVALID_HANDLE_VALUE;

static HMODULE g_tvicDll = NULL;
static TVIC_OPEN g_tvicOpen = NULL;
static TVIC_CLOSE g_tvicClose = NULL;
static TVIC_READPORT g_tvicReadPort = NULL;
static TVIC_WRITEPORT g_tvicWritePort = NULL;

//-------------------------------------------------------------------------
// startup message queue, drained by the engine once it can trace
//-------------------------------------------------------------------------
constexpr int INIT_MESSAGE_SLOTS = 8;
constexpr int INIT_MESSAGE_SIZE = 160;

static char g_initMessages[INIT_MESSAGE_SLOTS][INIT_MESSAGE_SIZE];
static int g_initMessageCount = 0;

static void
InitMessage(const char* fmt, ...) {
	if (g_initMessageCount >= INIT_MESSAGE_SLOTS) return;

	va_list args;
	va_start(args, fmt);
	vsprintf_s(g_initMessages[g_initMessageCount], INIT_MESSAGE_SIZE, fmt, args);
	va_end(args);

	g_initMessageCount++;
}

int
PortAccess_InitMessageCount(void) {
	return g_initMessageCount;
}

const char*
PortAccess_InitMessage(int index) {
	if (index < 0 || index >= g_initMessageCount) return "";
	return g_initMessages[index];
}

//-------------------------------------------------------------------------
// backend preference from the ini, read directly because the backend opens
// before the engine and its config parser exist
//-------------------------------------------------------------------------
enum BACKEND_PREFERENCE {
	PREFER_AUTO = 0,
	PREFER_PAWNIO,
	PREFER_TVICPORT,
};

static BACKEND_PREFERENCE
ReadBackendPreference(void) {
	FILE* f = NULL;
	if (fopen_s(&f, "TPFanControl.ini", "r") != 0 || !f)
		return PREFER_AUTO;

	BACKEND_PREFERENCE pref = PREFER_AUTO;
	char buf[256];

	while (fgets(buf, sizeof(buf), f)) {
		const char* p = buf;
		while (*p == ' ' || *p == '\t') p++;

		if (_strnicmp(p, "PortBackend=", 12) != 0) continue;
		p += 12;

		if (_strnicmp(p, "pawnio", 6) == 0) pref = PREFER_PAWNIO;
		else if (_strnicmp(p, "tvicport", 8) == 0) pref = PREFER_TVICPORT;
		break;
	}

	fclose(f);
	return pref;
}

//-------------------------------------------------------------------------
// PawnIO backend
//-------------------------------------------------------------------------
static bool
PawnIoExecute(const char* name, const ULONG64* in, DWORD inCount, ULONG64* out, DWORD outCount) {
	char input[PAWNIO_FN_NAME_LENGTH + 2 * sizeof(ULONG64)] = { 0 };

	if (inCount > 2) return false;

	strcpy_s(input, PAWNIO_FN_NAME_LENGTH, name);
	if (inCount)
		memcpy(input + PAWNIO_FN_NAME_LENGTH, in, inCount * sizeof(ULONG64));

	DWORD written = 0;
	if (!::DeviceIoControl(g_pawnio, PAWNIO_IOCTL_EXECUTE_FN,
			input, PAWNIO_FN_NAME_LENGTH + inCount * sizeof(ULONG64),
			out, outCount * sizeof(ULONG64), &written, NULL))
		return false;

	return written == outCount * sizeof(ULONG64);
}

static void
PawnIoClose(void) {
	if (g_pawnio != INVALID_HANDLE_VALUE) {
		::CloseHandle(g_pawnio);
		g_pawnio = INVALID_HANDLE_VALUE;
	}
}

static bool
PawnIoOpen(void) {
	// the documented device path; the DosDevices symlink is deprecated but
	// covers driver builds that still provide only that
	g_pawnio = ::CreateFileW(L"\\\\.\\GLOBALROOT\\Device\\PawnIO",
		GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, 0, NULL);

	if (g_pawnio == INVALID_HANDLE_VALUE)
		g_pawnio = ::CreateFileW(L"\\\\.\\PawnIO",
			GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING, 0, NULL);

	if (g_pawnio == INVALID_HANDLE_VALUE)
		return false;

	ULONG version = 0;
	DWORD written = 0;
	::DeviceIoControl(g_pawnio, PAWNIO_IOCTL_VERSION,
		NULL, 0, &version, sizeof(version), &written, NULL);

	FILE* f = NULL;
	if (fopen_s(&f, PAWNIO_MODULE_FILE, "rb") != 0 || !f) {
		InitMessage("PawnIO driver found but %s is missing next to the program", PAWNIO_MODULE_FILE);
		PawnIoClose();
		return false;
	}

	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	UCHAR* blob = NULL;
	bool loaded = false;

	if (size > 0 && size <= (long)PAWNIO_MAX_MODULE_SIZE) {
		blob = (UCHAR*)malloc(size);
		if (blob && fread(blob, 1, size, f) == (size_t)size) {
			DWORD ignored = 0;
			loaded = ::DeviceIoControl(g_pawnio, PAWNIO_IOCTL_LOAD_BINARY,
				blob, size, NULL, 0, &ignored, NULL) != 0;
		}
	}

	if (blob) free(blob);
	fclose(f);

	if (!loaded) {
		InitMessage("PawnIO refused %s (unsigned or damaged module?)", PAWNIO_MODULE_FILE);
		PawnIoClose();
		return false;
	}

	// a status read proves the module actually reaches the EC ports
	ULONG64 in = PAWNIO_EC_CTRLPORT;
	ULONG64 out = 0;
	if (!PawnIoExecute("ioctl_pio_read", &in, 1, &out, 1)) {
		InitMessage("PawnIO module loaded but a test read of port 0x66 failed");
		PawnIoClose();
		return false;
	}

	InitMessage("Port backend: PawnIO %lu.%lu.%lu with %s (EC at 0x62/0x66)",
		(version >> 16) & 0xFFFF, (version >> 8) & 0xFF, version & 0xFF,
		PAWNIO_MODULE_FILE);

	return true;
}

//-------------------------------------------------------------------------
// TVicPort backend
//-------------------------------------------------------------------------
static void
TVicClose(void) {
	if (g_tvicClose) g_tvicClose();

	if (g_tvicDll) {
		::FreeLibrary(g_tvicDll);
		g_tvicDll = NULL;
	}

	g_tvicOpen = NULL;
	g_tvicClose = NULL;
	g_tvicReadPort = NULL;
	g_tvicWritePort = NULL;
}

static bool
TVicOpen(void) {
	g_tvicDll = ::LoadLibrary("TVicPort.dll");
	if (!g_tvicDll)
		return false;

	g_tvicOpen = (TVIC_OPEN)::GetProcAddress(g_tvicDll, "OpenTVicPort");
	g_tvicClose = (TVIC_CLOSE)::GetProcAddress(g_tvicDll, "CloseTVicPort");
	g_tvicReadPort = (TVIC_READPORT)::GetProcAddress(g_tvicDll, "ReadPort");
	g_tvicWritePort = (TVIC_WRITEPORT)::GetProcAddress(g_tvicDll, "WritePort");

	if (!g_tvicOpen || !g_tvicClose || !g_tvicReadPort || !g_tvicWritePort || !g_tvicOpen()) {
		g_tvicClose = NULL;	// nothing was opened, so nothing to close
		TVicClose();
		return false;
	}

	InitMessage("Port backend: TVicPort");
	return true;
}

//-------------------------------------------------------------------------
// public surface
//-------------------------------------------------------------------------
bool
PortAccess_Open(void) {
	if (g_backend != PORTACCESS_NONE)
		return true;

	static bool preferenceRead = false;
	static BACKEND_PREFERENCE pref = PREFER_AUTO;
	if (!preferenceRead) {
		pref = ReadBackendPreference();
		preferenceRead = true;
	}

	// keep only the message from the attempt that decides the outcome; the
	// open retries for minutes at boot and would flood the queue otherwise
	g_initMessageCount = 0;

	if (pref != PREFER_TVICPORT && PawnIoOpen()) {
		g_backend = PORTACCESS_PAWNIO;
		return true;
	}

	if (pref != PREFER_PAWNIO && TVicOpen()) {
		g_backend = PORTACCESS_TVICPORT;
		return true;
	}

	return false;
}

void
PortAccess_Close(void) {
	switch (g_backend) {
	case PORTACCESS_PAWNIO:
		PawnIoClose();
		break;
	case PORTACCESS_TVICPORT:
		TVicClose();
		break;
	default:
		break;
	}

	g_backend = PORTACCESS_NONE;
}

PORTACCESS_BACKEND
PortAccess_Backend(void) {
	return g_backend;
}

const char*
PortAccess_BackendName(void) {
	switch (g_backend) {
	case PORTACCESS_PAWNIO: return "PawnIO";
	case PORTACCESS_TVICPORT: return "TVicPort";
	default: return "none";
	}
}

bool
PortAccess_AllowsPort(USHORT port) {
	switch (g_backend) {
	case PORTACCESS_PAWNIO:
		return port == PAWNIO_EC_DATAPORT || port == PAWNIO_EC_CTRLPORT;
	case PORTACCESS_TVICPORT:
		return true;
	default:
		return false;
	}
}

UCHAR
PortAccess_ReadByte(USHORT port) {
	switch (g_backend) {
	case PORTACCESS_PAWNIO: {
		ULONG64 in = port;
		ULONG64 out = 0;
		if (PawnIoExecute("ioctl_pio_read", &in, 1, &out, 1))
			return (UCHAR)out;
		break;
	}
	case PORTACCESS_TVICPORT:
		return g_tvicReadPort(port);
	default:
		break;
	}

	// reads like a floating bus, so the EC wait loops time out and trace
	return 0xFF;
}

void
PortAccess_WriteByte(USHORT port, UCHAR value) {
	switch (g_backend) {
	case PORTACCESS_PAWNIO: {
		ULONG64 in[2] = { port, value };
		PawnIoExecute("ioctl_pio_write", in, 2, NULL, 0);
		break;
	}
	case PORTACCESS_TVICPORT:
		g_tvicWritePort(port, value);
		break;
	default:
		break;
	}
}
