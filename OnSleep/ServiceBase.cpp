/****************************** Module Header ******************************\
* Module Name:  ServiceBase.cpp
* Project:      OnSleep
* Copyright (c) Microsoft Corporation.
* 
* Provides a base class for a service that will exist as part of a service 
* application. CServiceBase must be derived from when creating a new service 
* class.
* 
* This source is subject to the Microsoft Public License.
* See http://www.microsoft.com/en-us/openness/resources/licenses.aspx#MPL.
* All other rights reserved.
* 
* THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND, 
* EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED 
* WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR PURPOSE.
\***************************************************************************/

#pragma region Includes
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
#pragma endregion

using namespace boost;
using namespace boost::log;

#pragma region Static Members
class CServiceHandle {
public:
	CServiceHandle(SC_HANDLE hnd) {
		m_scHandle = hnd;
	}
	SC_HANDLE operator = (const SC_HANDLE hnd) {
		m_scHandle = hnd;
		return m_scHandle;
	}
	operator SC_HANDLE(){
		return m_scHandle;
	}
	~CServiceHandle() {
		if (NULL != m_scHandle)
			CloseServiceHandle(m_scHandle);
	}
private:
	SC_HANDLE m_scHandle=NULL;
};

// Initialize the singleton service instance.
CServiceBase *CServiceBase::s_service = NULL;
std::string from_unicode(std::wstring lstr) {
	return boost::locale::conv::from_utf(lstr, "UTF-8");
}
std::wstring to_unicode(std::string lstr) {
	return boost::locale::conv::to_utf<wchar_t>(lstr, "UTF-8");
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

    // Register the handler function for the service
    s_service->m_statusHandle = RegisterServiceCtrlHandlerEx(
        s_service->m_name, ServiceCtrlHandlerEx,NULL);
    if (s_service->m_statusHandle == NULL)
    {
        throw GetLastError();
    }
	//Sleep(10000);
	s_service->m_hLidSwitchNotify=RegisterPowerSettingNotification(s_service->m_statusHandle, &GUID_LIDSWITCH_STATE_CHANGE, DEVICE_NOTIFY_SERVICE_HANDLE);
	s_service->m_hPowerSrcNotify = RegisterPowerSettingNotification(s_service->m_statusHandle, &GUID_ACDC_POWER_SOURCE, DEVICE_NOTIFY_SERVICE_HANDLE);
    // Start the service.
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

#pragma endregion
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
DWORD CServiceBase::OnPowerEvent(DWORD dwEventType, LPVOID lpEventdata, LPVOID lpContext)
{
	TCHAR buff[1024];
	
	if (PBT_POWERSETTINGCHANGE == dwEventType) {
		POWERBROADCAST_SETTING* lpbs = (POWERBROADCAST_SETTING*)lpEventdata;
		if (GUID_ACDC_POWER_SOURCE == lpbs->PowerSetting)
		{
			this->bBattery = *(DWORD*)lpbs->Data;
		}
		if (GUID_LIDSWITCH_STATE_CHANGE == lpbs->PowerSetting)
		{
			this->bLid = *(DWORD*)lpbs->Data;
		}
		wsprintf(buff, L"Lid %d Battery %d", this->bLid, this->bBattery);
		s_service->WriteEventLogEntry(buff, EVENTLOG_INFORMATION_TYPE);
		if (0 == this->bLid && 1 == this->bBattery)
			OnSleep();
		if (1 == this->bLid)
			OnResume();

	}
	return NO_ERROR;
}
void CServiceBase::OnResume() {
	s_service->WriteEventLogEntry(L"Entering wakeup transition", EVENTLOG_INFORMATION_TYPE);
	s_service->StartServices();
}
void CServiceBase::OnSleep()
{
	s_service->WriteEventLogEntry(L"Entering sleep transition", EVENTLOG_INFORMATION_TYPE);
	s_service->StopServices();
}
#pragma region Service Constructor and Destructor



CServiceBase::CServiceBase(PWSTR pszServiceName, 
                           BOOL fCanStop, 
                           BOOL fCanShutdown)
{
    // Service name must be a valid string and cannot be NULL.
    m_name = (pszServiceName == NULL) ? L"" : pszServiceName;

    m_statusHandle = NULL;

    // The service runs in its own process.
    m_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;

    // The service is starting.
    m_status.dwCurrentState = SERVICE_START_PENDING;
	m_hPowerSrcNotify = NULL;
	m_hLidSwitchNotify = NULL;
    // The accepted commands of the service.
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
	vecServices.clear();
}



CServiceBase::~CServiceBase(void)
{
	CloseServiceHandle(hScm);
}

#pragma endregion





void CServiceBase::Start(DWORD dwArgc, PWSTR *pszArgv)
{
    try
    {
		hScm = OpenSCManager(NULL, NULL, GENERIC_ALL);
		if (NULL==hScm)
		{
			WriteErrorLogEntry(L"Unable to open SCM", GetLastError());
			throw std::runtime_error("Unable to open SCM");
		}
        // Tell SCM that the service is starting.
        SetServiceStatus(SERVICE_START_PENDING);
		winreg::RegKey key(HKEY_LOCAL_MACHINE, L"SOFTWARE\\OnSleep");
		auto vals = key.EnumValues();
		if (std::find(vals.begin(), vals.end(), std::make_pair(std::wstring(L"OnSleep"), REG_SZ)) != vals.end())
			ExecOnSleep=key.GetStringValue(L"OnSleep");
		if (std::find(vals.begin(), vals.end(), std::make_pair(std::wstring(L"OnWake"), REG_SZ)) != vals.end())
			ExecOnWake = key.GetStringValue(L"OnWake");

        // Tell SCM that the service is started.
        SetServiceStatus(SERVICE_RUNNING);
    }
    catch (DWORD dwError)
    {
        // Log the error.
        WriteErrorLogEntry(L"Service Start", dwError);

        // Set the service status to be stopped.
        SetServiceStatus(SERVICE_STOPPED, dwError);
    }

    catch (...)
    {
        // Log the error.
        WriteEventLogEntry(L"Service failed to start.", EVENTLOG_ERROR_TYPE);

        // Set the service status to be stopped.
        SetServiceStatus(SERVICE_STOPPED);
    }
}

void CServiceBase::SendStopSignals(std::wstring pwService)
{
	CServiceHandle hChild = OpenService(hScm, pwService.c_str(), SC_MANAGER_ALL_ACCESS);
	if (NULL == hChild)
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
	if (NULL == hChild)
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
	if (NULL == hChild)
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
	CServiceHandle hServ = OpenService(hScm, L"AudioEndpointBuilder", SC_MANAGER_ALL_ACCESS);
	DWORD BytesNeeded = 0;
	DWORD Services = 0;
	if (!EnumDependentServices(hServ, SERVICE_ACTIVE, NULL, 0, &BytesNeeded, &Services))
		if (!BytesNeeded)
			throw boost::log::runtime_error("Unable to enum services");
	LPENUM_SERVICE_STATUS DependedSvcs = (LPENUM_SERVICE_STATUS) new  byte[BytesNeeded];
	if (!EnumDependentServices(hServ, SERVICE_ACTIVE, DependedSvcs, BytesNeeded, &BytesNeeded, &Services))
		throw boost::log::runtime_error("Unable to enum services");

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
			if (GetLastError()!=ERROR_SERVICE_ALREADY_RUNNING)
				WriteErrorLogEntry(str(wformat(L"Unable to start service %s ") % sService).c_str(),GetLastError());
	}
}


void CServiceBase::Stop()
{
    DWORD dwOriginalState = m_status.dwCurrentState;
    try
    {
        // Tell SCM that the service is stopping.
        SetServiceStatus(SERVICE_STOP_PENDING);


        // Tell SCM that the service is stopped.
        SetServiceStatus(SERVICE_STOPPED);
    }
    catch (DWORD dwError)
    {
        // Log the error.
        WriteErrorLogEntry(L"Service Stop", dwError);

        // Set the orginal service status.
        SetServiceStatus(dwOriginalState);
    }
    catch (...)
    {
        // Log the error.
        WriteEventLogEntry(L"Service failed to stop.", EVENTLOG_ERROR_TYPE);

        // Set the orginal service status.
        SetServiceStatus(dwOriginalState);
    }
}


void CServiceBase::Shutdown()
{
    try
    {
        // Perform service-specific shutdown operations.

        // Tell SCM that the service is stopped.
        SetServiceStatus(SERVICE_STOPPED);
    }
    catch (DWORD dwError)
    {
        // Log the error.
        WriteErrorLogEntry(L"Service Shutdown", dwError);
    }
    catch (...)
    {
        // Log the error.
        WriteEventLogEntry(L"Service failed to shut down.", EVENTLOG_ERROR_TYPE);
    }
}

#pragma endregion



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

#pragma endregion