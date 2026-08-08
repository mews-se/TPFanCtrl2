#include "_prec.h"
#include "approot.h"
#include "fancontrol.h"
#include "TVicPort.h"
#include "sharedstate.h"

// unchanged from what the port open here always did, just named so the one
// place that cares about the result reads the same as the rest of the startup
static bool OpenPortDriver() {
    for (int i = 0; i < 180; i++) {
        if (OpenTVicPort())
            return true;

        ::Sleep(1000);
    }

    return false;
}

bool IsElevated() {
    HANDLE token;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;

    TOKEN_ELEVATION elev = { 0 };
    DWORD len = sizeof(elev);
    bool ok = ::GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &len) != 0;
    ::CloseHandle(token);

    return ok && elev.TokenIsElevated != 0;
}

// -1 not installed, otherwise the current SERVICE_ state
static int ServiceState() {
    SC_HANDLE mgr = ::OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (!mgr)
        return -1;

    SC_HANDLE svc = ::OpenService(mgr, g_ServiceName, SERVICE_QUERY_STATUS);
    SERVICE_STATUS st = { 0 };
    int state = (svc && ::QueryServiceStatus(svc, &st)) ? (int)st.dwCurrentState : -1;

    if (svc)
        ::CloseServiceHandle(svc);
    ::CloseServiceHandle(mgr);

    return state;
}

// so a window coming up at logon waits for the service instead of racing it
static bool ServiceIsUp() {
    int state = ServiceState();

    return state != -1 && state != SERVICE_STOPPED;
}

// where a service is installed it is the engine, so start it rather than take the fan
static void StartTheService() {
    if (ServiceState() != SERVICE_STOPPED)
        return;

    SC_HANDLE mgr = ::OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (!mgr)
        return;

    SC_HANDLE svc = ::OpenService(mgr, g_ServiceName, SERVICE_START);
    if (svc) {
        ::StartService(svc, 0, NULL);
        ::CloseServiceHandle(svc);
    }

    ::CloseServiceHandle(mgr);
}

bool RunSelfElevated(const char* args, bool wait) {
    char exe[MAX_PATH];
    if (!::GetModuleFileName(NULL, exe, MAX_PATH))
        return false;

    SHELLEXECUTEINFO sei = { sizeof(sei) };
    sei.fMask = wait ? SEE_MASK_NOCLOSEPROCESS : 0;
    sei.lpVerb = "runas";
    sei.lpFile = exe;
    sei.lpParameters = args;
    sei.nShow = SW_SHOWNORMAL;

    if (!::ShellExecuteEx(&sei))
        return false;

    if (wait && sei.hProcess) {
        ::WaitForSingleObject(sei.hProcess, 30000);
        ::CloseHandle(sei.hProcess);
    }

    return true;
}

int APIENTRY WinMain(HINSTANCE instance, HINSTANCE, LPSTR aArgs, int) {
    hInstRes = instance;
    hInstApp = instance;

    bool install = false;
    bool uninstall = false;
    bool quiet = false;
    bool debug = false;
    bool run = false;

    if (aArgs && *aArgs) {
        char *args = aArgs;
        while (*args) {
            if (*args == '-' || *args == '/') {
                ++args;
				switch (*args) {
				case 'i':
				case 'I': install = true; break;
				case 'u':
				case 'U': uninstall = true; break;
				case 'q':
				case 'Q': quiet = true; break;
				case 'd':
				case 'D': debug = true; break;
				case 's':
				case 'S': run = true; break;
				default: ShowHelp(); return -1;
                }
                ++args;
            }
            else if (*args == ' ') {
                ++args;
            }
            else {
                ShowHelp();
                return -1;
            }
        }
    }

    // Elevate only for what needs it: the port driver or the service. Decide
    // before taking the single instance lock, or the elevated copy collides with it.
    if (!run && !IsElevated()) {
        bool elevate = install || uninstall;

        // a client of a running engine needs no rights at all
        if (!elevate && !SharedState_EngineRunning()) {
            if (ServiceIsUp()) {
                // wait for the service rather than prompting: Windows silently
                // refuses an elevated run key entry at logon
                for (int i = 0; i < 240 && !SharedState_EngineRunning(); i++)
                    ::Sleep(500);

                // the service holds the fan either way, so leave quietly
                if (!SharedState_EngineRunning())
                    return 0;
            }
            else
                elevate = true;
        }

        if (elevate)
            return RunSelfElevated(aArgs, false) ? 0 : -1;
    }

    // -i/-u do one thing and exit, before the single instance lock is taken
    if (install)
        return InstallService(quiet);

    if (uninstall)
        return UninstallService(quiet);

	HANDLE hLock = CreateMutex(NULL,FALSE,"TPFanControlMutex01");

  if (hLock == NULL) {
      DWORD ec = GetLastError();
      ShowError(ec, "program or service already running");
	
      return ec;
  }

  if (WAIT_OBJECT_0 != WaitForSingleObject(hLock,0)) {
      DWORD ec = GetLastError();
      ShowError(ec, "program or service already running");

      return ec;
  }

    if (aArgs && *aArgs) {
		if (debug) {
			WorkerThread(NULL);
			return 0;
		}

		if (run) {
			g_isService = true;
			// HANDLE hLockS = CreateMutex(NULL,FALSE,"TPFanControlMutex02");
			SERVICE_TABLE_ENTRY svcEntry[2];
			svcEntry[0].lpServiceName = g_ServiceName;
			svcEntry[0].lpServiceProc = ServiceMain;
			svcEntry[1].lpServiceName = NULL;
			svcEntry[1].lpServiceProc = NULL;
			StartServiceCtrlDispatcher(svcEntry);
		}
    }
    else {
		WorkerThread(NULL);
		return 0;
    }

    return 0;
}

// A RUNASADMIN compatibility layer on this exe overrides the manifest, and
// Windows silently skips a run key entry that wants elevating. Older versions
// always asked for administrator, so one is usually set and it outlives them.
static void ClearRunAsAdminLayer(HKEY root) {
    char exe[MAX_PATH];
    if (!GetModuleFileName(NULL, exe, MAX_PATH))
        return;

    HKEY key;
    if (RegOpenKeyEx(root, "Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers",
        0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return;

    char val[512] = "";
    DWORD len = sizeof(val) - 1, type = 0;

    if (RegQueryValueEx(key, exe, NULL, &type, (LPBYTE)val, &len) == ERROR_SUCCESS && type == REG_SZ) {
        char keep[512] = "";
        char *ctx = NULL;

        // anything else set there was somebody's choice, only this flag comes out
        for (char *tok = strtok_s(val, " \t", &ctx); tok; tok = strtok_s(NULL, " \t", &ctx)) {
            if (_stricmp(tok, "RUNASADMIN") == 0 || strcmp(tok, "~") == 0)
                continue;
            if (*keep)
                strcat_s(keep, sizeof(keep), " ");
            strcat_s(keep, sizeof(keep), tok);
        }

        if (*keep) {
            char rest[512];
            sprintf_s(rest, sizeof(rest), "~ %s", keep);
            RegSetValueEx(key, exe, 0, REG_SZ, (const BYTE*)rest, (DWORD)strlen(rest) + 1);
        }
        else
            RegDeleteValue(key, exe);
    }

    RegCloseKey(key);
}

void ShowHelp() {
    MessageBox(NULL, "Usage:\n\n-i Install service (runs as SYSTEM, no window)\n-u Uninstall service\n-q Quiet - Don't show possible error messages", "Usage", MB_OK);
}

DWORD InstallService(bool quiet) {
    // the run key half cannot work while such a layer is set
    ClearRunAsAdminLayer(HKEY_CURRENT_USER);
    ClearRunAsAdminLayer(HKEY_LOCAL_MACHINE);

    SC_HANDLE SCMgr = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!SCMgr) {
        DWORD ec = GetLastError();
        if (!quiet) ShowError(ec, "Could not open Service Control Manager");
        return ec;
    }

    char exe[MAX_PATH], ExePath[MAX_PATH + 16];
    GetModuleFileName(NULL, exe, MAX_PATH);
	sprintf_s(ExePath, sizeof(ExePath), "\"%s\" -s", exe);

    SC_HANDLE svc = CreateService(SCMgr, g_ServiceName, g_ServiceName, SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
        ExePath, NULL, NULL, NULL, NULL, NULL);

    if (!svc) {
        CloseServiceHandle(SCMgr);
        DWORD ec = GetLastError();
        if (!quiet) ShowError(ec, "Could not install service");
        return ec;
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(SCMgr);

    return 0;
}

DWORD UninstallService(bool quiet) {
    SC_HANDLE SCMgr = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!SCMgr) {
        DWORD ec = GetLastError();
        if (!quiet) ShowError(ec, "Could not open Service Control Manager");
        return ec;
    }

    SC_HANDLE hdl = OpenService(SCMgr, g_ServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);
    if (!hdl) {
        CloseServiceHandle(SCMgr);
        return 0;
    }

    // Stop it first. DeleteService on a running service only marks it for
    // deletion, it keeps running and keeps driving the fan until it goes away.
    SERVICE_STATUS status = { 0 };
    if (ControlService(hdl, SERVICE_CONTROL_STOP, &status)) {
        for (int i = 0; i < 40 && status.dwCurrentState != SERVICE_STOPPED; i++) {
            ::Sleep(500);
            if (!QueryServiceStatus(hdl, &status)) break;
        }
    }

    if (!DeleteService(hdl)) {
        DWORD ec = GetLastError();
        if (!quiet) ShowError(ec, "Could not delete service");
        CloseServiceHandle(SCMgr);
        return ec;
    }

    CloseServiceHandle(hdl);
    CloseServiceHandle(SCMgr);

    return 0;
}

void ShowError(DWORD ec, const char *description) { 
    char *msgBuf;

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM,
        NULL,
        ec,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR) &msgBuf,
        0, NULL );

    size_t dispBuf_len = strlen(msgBuf) + strlen(description) + 40;
    char *dispBuf = (char *)LocalAlloc(LMEM_ZEROINIT, dispBuf_len); 
    sprintf_s(dispBuf, dispBuf_len, "%s, error code %d: %s", description, ec, msgBuf); 
    MessageBox(NULL, dispBuf, "Error", MB_OK); 

    LocalFree(msgBuf);
    LocalFree(dispBuf);
}

void ShowMessage(const char *title, const char *description) { 
    MessageBox(NULL, description, title, MB_OK);
}

HANDLE CreateSharedEvent(const char* name) {
    SECURITY_ATTRIBUTES sa;
    if (!SharedSecurity(&sa))
        return ::CreateEvent(NULL, TRUE, FALSE, name);

    HANDLE h = ::CreateEvent(&sa, TRUE, FALSE, name);
    ::LocalFree(sa.lpSecurityDescriptor);

    return h;
}

VOID WINAPI ServiceMain(DWORD aArgc, LPTSTR* aArgv) {
    g_SvcHandle = RegisterServiceCtrlHandlerEx(g_ServiceName, Handler, NULL);

    g_SvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_SvcStatus.dwCurrentState = SERVICE_START_PENDING;
    g_SvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_SvcStatus.dwWin32ExitCode = NO_ERROR;
    g_SvcStatus.dwServiceSpecificExitCode = NO_ERROR;
    g_SvcStatus.dwCheckPoint = 0;
    g_SvcStatus.dwWaitHint = 0;
    SetServiceStatus(g_SvcHandle, &g_SvcStatus);

    g_SvcStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(g_SvcHandle, &g_SvcStatus);

    // One context, SYSTEM, for as long as the service runs. Nothing is
    // started in anyone's session, so there is never a second one.
    StartWorkerThread();

    return;
}

DWORD WINAPI Handler(DWORD fdwControl, DWORD evtType, LPVOID evtData, LPVOID ctx) {
    switch(fdwControl) {
       case SERVICE_CONTROL_STOP:
            g_SvcStatus.dwCurrentState = SERVICE_STOP_PENDING;
            g_SvcStatus.dwWaitHint = 20000;
            SetServiceStatus(g_SvcHandle, &g_SvcStatus);

            StopWorkerThread();
                
            g_SvcStatus.dwCurrentState = SERVICE_STOPPED;
            g_SvcStatus.dwWaitHint = 0;
            SetServiceStatus(g_SvcHandle, &g_SvcStatus);

            break;

        default:
            break;
    }

    return NO_ERROR;
}

// _beginthreadex, so the handle stays ours across a start/stop handover
static unsigned __stdcall WorkerThreadEx(void* p) {
    WorkerThread(p);
    return 0;
}

void StartWorkerThread() {
    if (!g_workerThread)
        g_workerThread = (HANDLE)_beginthreadex(NULL, 0, WorkerThreadEx, NULL, 0, NULL);
}

bool StopWorkerThread() {
    if (!g_workerThread)
        return true;

    ::PostMessage(g_dialogWnd, WM_COMMAND, 5020, 0);

    // bounded, it may still be waiting for the port driver and have no window yet
	if (::WaitForSingleObject(g_workerThread, 15000) != WAIT_OBJECT_0)
        return false;

	::CloseHandle(g_workerThread);
    g_workerThread = NULL;

    return true;
}

void WorkerThread(void *dummy) {
	char curdir[MAX_PATH]= "";
	
	//   #ifdef _DEBUG   
	//   Sleep(30000);
	//   #endif

	hInstRes=GetModuleHandle(NULL);
	hInstApp=hInstRes;

	::InitCommonControls();

	// Change to the directory where the exe resides
	char exepath[MAX_PATH];
	*exepath = '\0';
	if (GetModuleFileName(NULL, exepath, MAX_PATH))	{
		char *p = exepath + strlen(exepath) - 1;
		while (p > exepath) {
			if (*p == '\\')	{
				*p = '\0';
				::SetCurrentDirectory(exepath);
				break;
			}
			--p;
		}
	}

	// hand over to the service if one is installed but stopped
	if (!g_isService) {
		StartTheService();

		for (int i = 0; i < 60 && ServiceIsUp() && !SharedState_EngineRunning(); i++)
			::Sleep(500);
	}

	// the startup mutex is session local; session 0 and the user's session need a global one
	HANDLE running = CreateSharedMutex("Global\\TPFanControl_Running");

	if (running) {
		DWORD wait = ::WaitForSingleObject(running, 0);
		// abandoned means the last one died without releasing, so it is ours
		if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
			::CloseHandle(running);
			running = NULL;

			// a service with nothing to show and no fan to drive just leaves
			if (g_isService) {
				g_SvcStatus.dwCurrentState = SERVICE_STOPPED;
				::SetServiceStatus(g_SvcHandle, &g_SvcStatus);
				return;
			}

			// come up as a client of the owner: no backend, so no fan writes
			g_clientMode = true;
		}
	}

	if (!g_clientMode) {
		// an engine is elevated, or is the service and runs as SYSTEM, so this
		// is where a layer left over from an upgrade in place gets cleared
		ClearRunAsAdminLayer(HKEY_CURRENT_USER);
		ClearRunAsAdminLayer(HKEY_LOCAL_MACHINE);

		// a stale close request would shut this one down as it starts
		HANDLE stale = CreateSharedEvent("Global\\TPFanControl_Close");
		if (stale) {
			::ResetEvent(stale);
			::CloseHandle(stale);
		}
	}

	// Only an engine opens the backend, and it publishes before it does, so a
	// window starting alongside it at logon can attach straight away.
	bool ready = g_clientMode ? SharedState_Attach()
		: (SharedState_Create() && OpenPortDriver());

	// release the fan before the error box, or later instances attach to nothing
	if (!ready && running) {
		::ReleaseMutex(running);
		::CloseHandle(running);
		running = NULL;
	}

	if (ready) {
		FANCONTROL fc(hInstApp);

        g_dialogWnd = fc.GetDialogWnd();

		fc.ProcessDialog();

		::PostMessage(g_dialogWnd, WM_COMMAND, 5020, 0);
	}
	else if (g_clientMode) {
		// the engine went away between deciding to attach to it and doing so
		::MessageBox(HWND_DESKTOP,
					"The instance that owns the fan went away while this\r\n"
					"window was starting. Start the program again.",
					"Fan Control",
					MB_ICONERROR | MB_OK | MB_SETFOREGROUND);
	}
	else if (!g_isService) {
		// no message box in session 0, nobody can answer it
		::MessageBox(HWND_DESKTOP,
					"Error during initialization of Port Driver.\r\n"
					"(tvicport.sys missing in app folder or failed to load)",
					"Fan Control",
					MB_ICONERROR | MB_OK | MB_SETFOREGROUND);
	}

	SharedState_Close();
	CloseTVicPort();

	if (running) {
		::ReleaseMutex(running);
		::CloseHandle(running);
	}

	// the worker can end on its own, so report STOPPED or the service cannot restart
	if (g_isService && g_SvcHandle && g_SvcStatus.dwCurrentState != SERVICE_STOPPED) {
		g_SvcStatus.dwCurrentState = SERVICE_STOPPED;
		::SetServiceStatus(g_SvcHandle, &g_SvcStatus);
	}
}

void debug(const char *msg) {
	FILE *flog;

    errno_t errflog = fopen_s(&flog,"fancontrol_debug.log", "ab");
	if (!errflog) {
		fwrite(msg, strlen(msg), 1, flog); 
		fclose(flog);
	}
}
