#include "ServiceBase.h"
#include <assert.h>
#include <strsafe.h>
#include <string>
#include <stdexcept>
#include <boost/log/exceptions.hpp>
#include <boost/format.hpp>
#include <boost/locale.hpp>
#include <boost/filesystem.hpp>
#include "WinReg.hpp"
#include <initguid.h>
#include <ole2.h>
#include <mstask.h>
#include <msterr.h>
#include <wchar.h>
#include <stdio.h>
#include <taskschd.h>
#include <comdef.h>
#pragma comment(lib, "taskschd.lib")

using namespace boost;
using namespace boost::log;



// Initialize the singleton service instance.
CServiceBase *CServiceBase::s_service = NULL;

std::string from_unicode(std::wstring lstr) {
	return boost::locale::conv::from_utf(lstr, "UTF-8");
}
std::wstring to_unicode(std::string lstr) {
	return boost::locale::conv::to_utf<wchar_t>(lstr, "UTF-8");
}
std::wstring GuidToString(GUID guid)
{
	wchar_t guid_cstr[39];
	wsprintf(guid_cstr,
		L"{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
		guid.Data1, guid.Data2, guid.Data3,
		guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
		guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);

	return std::wstring(guid_cstr);
}
BOOL CServiceBase::Run(CServiceBase &service)
{
    s_service = &service;

    SERVICE_TABLE_ENTRY serviceTable[] = 
    {
        { service.m_name, ServiceMain },
        { NULL, NULL }
    };

    // Connects the main thread of a service process to the service control 
    // manager, which causes the thread to be the service control dispatcher 
    // thread for the calling process. This call returns when the service has 
    // stopped. The process should simply terminate when the call returns.
    return StartServiceCtrlDispatcher(serviceTable);
}


void WINAPI CServiceBase::ServiceMain(DWORD dwArgc, PWSTR *pszArgv)
{
    assert(s_service != NULL);

    s_service->m_statusHandle = RegisterServiceCtrlHandlerEx(
        s_service->m_name, ServiceCtrlHandlerEx,NULL);
    if (s_service->m_statusHandle == NULL)
    {
		throw runtime_error(str(format("Unable to start service %d") % GetLastError()).c_str());
    }
    s_service->Start(dwArgc, pszArgv);

}

DWORD  WINAPI CServiceBase::ServiceCtrlHandlerEx(DWORD dwCtrl,DWORD dwEventType,LPVOID lpEventdata,LPVOID lpContext)
{
    switch (dwCtrl)
    {
		case SERVICE_CONTROL_STOP: s_service->Stop(); break;
		case SERVICE_CONTROL_SHUTDOWN: s_service->Shutdown(); break;
		case SERVICE_CONTROL_INTERROGATE: break;
		case SERVICE_CONTROL_POWEREVENT: return s_service->OnPowerEvent(dwEventType, lpEventdata, lpContext); break;
		default: break;
    }
	return NO_ERROR;
}

DWORD CServiceBase::OnPowerEvent(DWORD dwEventType, LPVOID lpEventdata, LPVOID lpContext)
{	
	if (PBT_POWERSETTINGCHANGE == dwEventType) {
		s_service->m_PowerCs.Lock();
		try {
			POWERBROADCAST_SETTING* lpbs = (POWERBROADCAST_SETTING*)lpEventdata;
			if (GUID_ACDC_POWER_SOURCE == lpbs->PowerSetting)
			{
				this->bBattery = *(DWORD*)lpbs->Data;
			}
			if (GUID_LIDSWITCH_STATE_CHANGE == lpbs->PowerSetting)
			{
				this->bLid = *(DWORD*)lpbs->Data;
			}
			s_service->WriteEventLogEntry(str(format("Lid %d Battery %d") % this->bLid % this->bBattery).c_str(), EVENTLOG_INFORMATION_TYPE);
			if (0 == this->bLid && 1 == this->bBattery)
				OnSleep();
			if (1 == this->bLid)
				OnWake();
		}
		catch (...)
		{

		}
		s_service->m_PowerCs.Unlock();
	}
	return NO_ERROR;
}
void CServiceBase::OnWake() {
	try {
		WriteEventLogEntry(L"Entering wakeup transition", EVENTLOG_INFORMATION_TYPE);
		StartServices();
	}
	catch (...) {
		WriteErrorLogEntry(L"Exception during wake transition", GetLastError());
	}
}
void CServiceBase::OnSleep()
{

	try {
		WriteEventLogEntry(L"Entering sleep transition", EVENTLOG_INFORMATION_TYPE);
		StopServices();
	}
	catch (...) {
		WriteErrorLogEntry(L"Exception during sleep transition", GetLastError());

	}
}



CServiceBase::CServiceBase(PWSTR pszServiceName, 
                           BOOL fCanStop, 
                           BOOL fCanShutdown)
{
    m_name = (pszServiceName == NULL) ? L"" : pszServiceName;
    m_statusHandle = NULL;
    m_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    m_status.dwCurrentState = SERVICE_START_PENDING;
    DWORD dwControlsAccepted = 0;
    if (fCanStop) 
        dwControlsAccepted |= SERVICE_ACCEPT_STOP;
    if (fCanShutdown) 
        dwControlsAccepted |= SERVICE_ACCEPT_SHUTDOWN;
	dwControlsAccepted |= SERVICE_ACCEPT_POWEREVENT;
    m_status.dwControlsAccepted = dwControlsAccepted;
    m_status.dwWin32ExitCode = NO_ERROR;
    m_status.dwServiceSpecificExitCode = 0;
    m_status.dwCheckPoint = 0;
    m_status.dwWaitHint = 0;

}


CServiceBase::~CServiceBase(void)
{
	UnregPowerNotification();
}

void CServiceBase::Start(DWORD dwArgc, PWSTR *pszArgv)
{
    try
    {
		hScm = OpenSCManager(NULL, NULL, GENERIC_ALL);
		if (hScm.empty())
		{
			WriteErrorLogEntry(L"Unable to open SCM", GetLastError());
			throw runtime_error("Unable to open SCM");
		}
        // Tell SCM that the service is starting.
        SetServiceStatus(SERVICE_START_PENDING);

		vecServices.clear();
		winreg::RegKey key(HKEY_LOCAL_MACHINE, L"SOFTWARE\\OnSleep");
		auto vals = key.EnumValues();
		if (std::find(vals.begin(), vals.end(), std::make_pair(std::wstring(L"OnSleep"), REG_SZ)) != vals.end())
			ExecOnSleep = key.GetStringValue(L"OnSleep");
		if (std::find(vals.begin(), vals.end(), std::make_pair(std::wstring(L"OnWake"), REG_SZ)) != vals.end())
			ExecOnWake = key.GetStringValue(L"OnWake");

		if (!RegPowerNotification())
			throw runtime_error("Unable to register Power Notification");
        SetServiceStatus(SERVICE_RUNNING);
    }
	catch (runtime_error e)
	{
		DWORD dwOldError = GetLastError();
		WriteErrorLogEntry(str(format("Unable to initialize service %s") % e.what()).c_str());
		SetServiceStatus(SERVICE_STOPPED, dwOldError);
	}
    catch (...)
    {
        WriteEventLogEntry(L"Service failed to start.", EVENTLOG_ERROR_TYPE);
        SetServiceStatus(SERVICE_STOPPED);
    }
}

void CServiceBase::SendStopSignals(std::wstring pwService)
{
	CServiceHandle hChild = OpenService(hScm, pwService.c_str(), SC_MANAGER_ALL_ACCESS);
	if (hChild.empty())
		throw runtime_error(from_unicode(str(wformat(L"Unable to open service %s with %d") % pwService % GetLastError())));
	SERVICE_STATUS tStatus;
	if (!ControlService(hChild, SERVICE_CONTROL_STOP, &tStatus))
	{
		DWORD dwE = GetLastError();
		if (dwE != ERROR_SERVICE_NOT_ACTIVE && dwE!= ERROR_DEPENDENT_SERVICES_RUNNING)
		{
			throw runtime_error(from_unicode(str(wformat(L"Unable to stop service %s with %d") % pwService % GetLastError())));
		}
	}
}
DWORD CServiceBase::GetServiceState(std::wstring sName)
{
	CServiceHandle hChild = OpenService(hScm, sName.c_str(), SC_MANAGER_ALL_ACCESS);
	if (hChild.empty())
		throw runtime_error(from_unicode(str(wformat(L"Unable to open service %s with %d") % sName % GetLastError())));
	SERVICE_STATUS tStatus;
	if (!QueryServiceStatus(hChild, &tStatus))
	{
		if (GetLastError() != ERROR_SERVICE_NOT_ACTIVE)
		{
			throw runtime_error(from_unicode(str(wformat(L"Unable query service %s with %d") % sName % GetLastError())));
		}
	}
	return tStatus.dwCurrentState;
}
void CServiceBase::KillService(std::wstring sName) {
	CServiceHandle hChild = OpenService(hScm, sName.c_str(), SC_MANAGER_ALL_ACCESS);
	if (hChild.empty())
		throw runtime_error(from_unicode(str(wformat(L"Unable to open service %s with %d") % sName % GetLastError())));
	SERVICE_STATUS_PROCESS ssp;
	DWORD pBytesNeeded = 0;
	if (!QueryServiceStatusEx(hChild, SC_STATUS_PROCESS_INFO, (LPBYTE)& ssp, sizeof(ssp), &pBytesNeeded))
	{
		throw runtime_error(from_unicode(str(wformat(L"Unable to query service %s with %d") % sName % GetLastError())));
	}
	HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, false, ssp.dwProcessId);
	if (NULL == hProcess)
	{
		throw runtime_error(from_unicode(str(wformat(L"Unable to open process %d with %d") % ssp.dwProcessId % GetLastError())));
	}
	if (!TerminateProcess(hProcess, 0))
	{
		CloseHandle(hProcess);
		throw runtime_error(from_unicode(str(wformat(L"Unable to terminate process %d with %d") % ssp.dwProcessId % GetLastError())));
	}
	CloseHandle(hProcess);
}
void CServiceBase::StopServices()
{
	if (ExecOnSleep.length() > 0)
	{
		RunScheduledTask(ExecOnSleep);
	}
	CServiceHandle hServ = OpenService(hScm, L"AudioEndpointBuilder", SC_MANAGER_ALL_ACCESS);
	if (hServ.empty())
		throw runtime_error("Unable to open main service");
	
	DWORD BytesNeeded = 0, Services = 0;

	if (!EnumDependentServices(hServ, SERVICE_ACTIVE, NULL, 0, &BytesNeeded, &Services))
		if (!BytesNeeded)
			throw runtime_error("Unable to enum services");
	LPENUM_SERVICE_STATUS DependedSvcs = (LPENUM_SERVICE_STATUS) new  byte[BytesNeeded];
	if (!EnumDependentServices(hServ, SERVICE_ACTIVE, DependedSvcs, BytesNeeded, &BytesNeeded, &Services))
		throw runtime_error("Unable to enum services");

	//Send stop signals to services
	vecServices.push_back(L"AudioEndpointBuilder");
	for (u_int i = 0; i < Services; i++)
	{
			vecServices.push_back(DependedSvcs[i].lpServiceName);
	}
	delete[] DependedSvcs;
	bool bHaveRunningSvcs = false;
	for (int i=0;i<30;i++)
	{
		for each (std::wstring scName in vecServices)
		{
			try {
				SendStopSignals(scName);
				if (GetServiceState(scName) != SERVICE_STOPPED)
					bHaveRunningSvcs = true;
			}
			catch (runtime_error e)
			{
				WriteErrorLogEntry(e.what(), GetLastError());
			}
		}
		if (!bHaveRunningSvcs)
			break;
		else
			Sleep(500);
	} 
	for (auto& sService : vecServices)
	{
		if (GetServiceState(sService) != SERVICE_STOPPED)
		{
			WriteEventLogEntry(str(wformat(L"Service %s still running killing it") % sService).c_str());
			try {
				KillService(sService);
			}
			catch (runtime_error e)
			{
				WriteErrorLogEntry(str(wformat(L"Unable to terminate service %s \n %s") % sService % to_unicode(e.what())).c_str(),GetLastError());
			}
		}
	}
}
void CServiceBase::StartServices() {
	for (auto& sService : vecServices)
	{
		CServiceHandle hService = OpenService(hScm, sService.c_str(), SERVICE_ALL_ACCESS);
		if (!StartService(hService, NULL, NULL))
			if (GetLastError() != ERROR_SERVICE_ALREADY_RUNNING)
				WriteErrorLogEntry(str(wformat(L"Unable to start service %s ") % sService).c_str(), GetLastError());
	}
	if (ExecOnWake.length() > 0)
	{
		RunScheduledTask(ExecOnWake);
	}
}

bool CServiceBase::RunScheduledTask(std::wstring Taskname) {
	try {
		HRESULT hr = S_OK;
		_COM_SMARTPTR_TYPEDEF(ITaskService, __uuidof(ITaskService));
		_COM_SMARTPTR_TYPEDEF(ITaskFolder, __uuidof(ITaskFolder));
		_COM_SMARTPTR_TYPEDEF(IRegisteredTask, __uuidof(IRegisteredTask));
		_COM_SMARTPTR_TYPEDEF(IRunningTask, __uuidof(IRunningTask));
		ITaskServicePtr pITS;

		hr = CoInitialize(NULL);
		if (FAILED(hr))
		{
			throw runtime_error("Unable to Initialize COM");
		}
		hr = CoCreateInstance(CLSID_TaskScheduler,
			NULL,
			CLSCTX_INPROC_SERVER,
			IID_ITaskService,
			(void**)& pITS);
		if (FAILED(hr))
		{
			throw runtime_error("Unable to create  TaskScheduler object ");
		}
		hr = pITS->Connect(_variant_t(), _variant_t(),
			_variant_t(), _variant_t());
		if (FAILED(hr))
		{
			throw runtime_error(str(format("ITaskService::Connect failed: %x") % hr).c_str());
			
		}
		ITaskFolderPtr pRootFolder = NULL;
		hr = pITS->GetFolder(_bstr_t(L"\\"), &pRootFolder);
		if (FAILED(hr))
		{
			throw runtime_error(str(format("Cannot get Root Folder pointer: %x") % hr).c_str());
		}
		IRegisteredTaskPtr pITask;
		hr = pRootFolder->GetTask(_bstr_t(Taskname.c_str()), &pITask);
		if (FAILED(hr))
		{
			throw runtime_error(str(format("GetTask failed: %x") % hr).c_str());
		}
		IRunningTaskPtr pRunTask;
		hr = pITask->Run(_variant_t(), &pRunTask);
		if (FAILED(hr)) {
			throw runtime_error(str(format("RunTask failed: %x") % hr).c_str());
		}
	}
	catch (runtime_error e) {
		WriteErrorLogEntry(e.what());
		return false;
	}
	CoUninitialize();
	return true;
}
bool CServiceBase::RegPowerNotification() {
	m_hLidSwitchNotify = RegisterPowerSettingNotification(s_service->m_statusHandle, &GUID_LIDSWITCH_STATE_CHANGE, DEVICE_NOTIFY_SERVICE_HANDLE);
	if (NULL == m_hLidSwitchNotify)
		return false;
	m_hPowerSrcNotify = RegisterPowerSettingNotification(s_service->m_statusHandle, &GUID_ACDC_POWER_SOURCE, DEVICE_NOTIFY_SERVICE_HANDLE);
	if (NULL == m_hPowerSrcNotify)
	{
		UnregPowerNotification();
		return false;
	}
	return true;
}
void CServiceBase::UnregPowerNotification() {
	if (NULL != m_hPowerSrcNotify) {
		UnregisterPowerSettingNotification(m_hPowerSrcNotify);
		m_hPowerSrcNotify = NULL;
	}
	if (NULL != m_hLidSwitchNotify) {
		UnregisterPowerSettingNotification(m_hLidSwitchNotify);
		m_hLidSwitchNotify = NULL;
	}
}
void CServiceBase::Stop()
{
    DWORD dwOriginalState = m_status.dwCurrentState;
    try
    {
        SetServiceStatus(SERVICE_STOP_PENDING);
		UnregPowerNotification();
        SetServiceStatus(SERVICE_STOPPED);
    }
    catch (DWORD dwError)
    {
        WriteErrorLogEntry(L"Service Stop", dwError);
        SetServiceStatus(dwOriginalState);
    }
    catch (...)
    {
        WriteEventLogEntry(L"Service failed to stop.", EVENTLOG_ERROR_TYPE);
        SetServiceStatus(dwOriginalState);
    }
}


void CServiceBase::Shutdown()
{
    try
    {
        SetServiceStatus(SERVICE_STOPPED);
    }
    catch (DWORD dwError)
    {
        WriteErrorLogEntry(L"Service Shutdown", dwError);
    }
    catch (...)
    {
        WriteEventLogEntry(L"Service failed to shut down.", EVENTLOG_ERROR_TYPE);
    }
}

void CServiceBase::SetServiceStatus(DWORD dwCurrentState, 
                                    DWORD dwWin32ExitCode, 
                                    DWORD dwWaitHint)
{
    static DWORD dwCheckPoint = 1;

    // Fill in the SERVICE_STATUS structure of the service.

    m_status.dwCurrentState = dwCurrentState;
    m_status.dwWin32ExitCode = dwWin32ExitCode;
    m_status.dwWaitHint = dwWaitHint;

    m_status.dwCheckPoint = 
        ((dwCurrentState == SERVICE_RUNNING) ||
        (dwCurrentState == SERVICE_STOPPED)) ? 
        0 : dwCheckPoint++;

    // Report the status of the service to the SCM.
    ::SetServiceStatus(m_statusHandle, &m_status);
}


void CServiceBase::WriteEventLogEntry(PCSTR pszMessage, WORD wType)
{
	WriteEventLogEntry(to_unicode(pszMessage).c_str(), wType);
}
void CServiceBase::WriteErrorLogEntry(PCSTR pszMessage, DWORD dwError)
{
	WriteErrorLogEntry(to_unicode(pszMessage).c_str(), dwError);
}

void CServiceBase::WriteEventLogEntry(PCWSTR pszMessage, WORD wType)
{
    HANDLE hEventSource = NULL;
    LPCWSTR lpszStrings[2] = { NULL, NULL };

    hEventSource = RegisterEventSource(NULL, m_name);
    if (hEventSource)
    {
        lpszStrings[0] = m_name;
        lpszStrings[1] = pszMessage;

        ReportEvent(hEventSource,  // Event log handle
            wType,                 // Event type
            0,                     // Event category
            0,                     // Event identifier
            NULL,                  // No security identifier
            2,                     // Size of lpszStrings array
            0,                     // No binary data
            lpszStrings,           // Array of strings
            NULL                   // No binary data
            );

        DeregisterEventSource(hEventSource);
    }
}


void CServiceBase::WriteErrorLogEntry(PCWSTR pszFunction, DWORD dwError)
{
    wchar_t szMessage[260];
    StringCchPrintf(szMessage, ARRAYSIZE(szMessage), 
        L"%s failed w/err 0x%08lx", pszFunction, dwError);
    WriteEventLogEntry(szMessage, EVENTLOG_ERROR_TYPE);
}

