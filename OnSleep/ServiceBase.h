#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include "CServiceHandle.h"
#include "CriticalSection.h"

class CServiceBase
{
public:

    static BOOL Run(CServiceBase &service);

    CServiceBase(PWSTR pszServiceName, 
        BOOL fCanStop = TRUE, 
        BOOL fCanShutdown = TRUE);
    virtual ~CServiceBase(void);
    void Stop();

protected:
    void SetServiceStatus(DWORD dwCurrentState, 
        DWORD dwWin32ExitCode = NO_ERROR, 
        DWORD dwWaitHint = 0);
    void WriteEventLogEntry(PCWSTR pszMessage, WORD wType = EVENTLOG_INFORMATION_TYPE);
	void WriteEventLogEntry(PCSTR pszMessage, WORD wType = EVENTLOG_INFORMATION_TYPE);
    void WriteErrorLogEntry(PCWSTR pszFunction, 
        DWORD dwError = GetLastError());
	void WriteErrorLogEntry(PCSTR pszFunction,
		DWORD dwError = GetLastError());

private:
    static void WINAPI ServiceMain(DWORD dwArgc, LPWSTR *lpszArgv);
	static DWORD WINAPI ServiceCtrlHandlerEx(DWORD dwCtrl, DWORD dwEventType, LPVOID lpEventdata, LPVOID lpContext);
    void Start(DWORD dwArgc, PWSTR *pszArgv);
    void Shutdown();
    static CServiceBase *s_service;
    PWSTR m_name;
    SERVICE_STATUS m_status;
    SERVICE_STATUS_HANDLE m_statusHandle;
	HPOWERNOTIFY m_hLidSwitchNotify=NULL;
	HPOWERNOTIFY m_hPowerSrcNotify=NULL;
	DWORD bBattery = -1;
	DWORD bLid = -1;
	DWORD CServiceBase::OnPowerEvent(DWORD dwEventType, LPVOID lpEventdata, LPVOID lpContext);
	bool RegPowerNotification();
	void UnregPowerNotification();
	void OnSleep();
	CServiceHandle hScm = NULL;
	void StopServices();
	void SendStopSignals(std::wstring pwService);
	std::vector<std::wstring> vecServices;
	DWORD  GetServiceState(std::wstring sName);
	void KillService(std::wstring sName);
	void OnWake();
	std::wstring ExecOnSleep = L"";
	std::wstring ExecOnWake = L"";;
	CCriticalSection m_PowerCs;
	bool RunScheduledTask(std::wstring Taskname);
	void StartServices();
};