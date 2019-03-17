/****************************** Module Header ******************************\
* Module Name:  ServiceBase.h
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

#pragma once

#include <windows.h>
#include <string>
#include <vector>


class CServiceBase
{
public:

    // Register the executable for a service with the Service Control Manager 
    // (SCM). After you call Run(ServiceBase), the SCM issues a Start command, 
    // which results in a call to the OnStart method in the service. This 
    // method blocks until the service has stopped.
    static BOOL Run(CServiceBase &service);

    // Service object constructor. The optional parameters (fCanStop, 
    // fCanShutdown and fCanPauseContinue) allow you to specify whether the 
    // service can be stopped, paused and continued, or be notified when 
    // system shutdown occurs.
    CServiceBase(PWSTR pszServiceName, 
        BOOL fCanStop = TRUE, 
        BOOL fCanShutdown = TRUE);

    // Service object destructor. 
    virtual ~CServiceBase(void);

    // Stop the service.
    void Stop();

protected:

    // Set the service status and report the status to the SCM.
    void SetServiceStatus(DWORD dwCurrentState, 
        DWORD dwWin32ExitCode = NO_ERROR, 
        DWORD dwWaitHint = 0);

    // Log a message to the Application event log.
    void WriteEventLogEntry(PCWSTR pszMessage, WORD wType = EVENTLOG_INFORMATION_TYPE);
	// Log a message to the Application event log.
	void WriteEventLogEntry(PCSTR pszMessage, WORD wType = EVENTLOG_INFORMATION_TYPE);

    // Log an error message to the Application event log.
    void WriteErrorLogEntry(PCWSTR pszFunction, 
        DWORD dwError = GetLastError());
	void WriteErrorLogEntry(PCSTR pszFunction,
		DWORD dwError = GetLastError());

private:

    // Entry point for the service. It registers the handler function for the 
    // service and starts the service.
    static void WINAPI ServiceMain(DWORD dwArgc, LPWSTR *lpszArgv);

    // The function is called by the SCM whenever a control code is sent to 
    // the service.
	static DWORD WINAPI ServiceCtrlHandlerEx(DWORD dwCtrl, DWORD dwEventType, LPVOID lpEventdata, LPVOID lpContext);

    // Start the service.
    void Start(DWORD dwArgc, PWSTR *pszArgv);


    // Execute when the system is shutting down.
    void Shutdown();

    // The singleton service instance.
    static CServiceBase *s_service;

    // The name of the service
    PWSTR m_name;

    // The status of the service
    SERVICE_STATUS m_status;

    // The service status handle
    SERVICE_STATUS_HANDLE m_statusHandle;
	HPOWERNOTIFY m_hLidSwitchNotify;
	HPOWERNOTIFY m_hPowerSrcNotify;
	DWORD bBattery = -1;
	DWORD bLid = -1;
	DWORD CServiceBase::OnPowerEvent(DWORD dwEventType, LPVOID lpEventdata, LPVOID lpContext);
	void OnSleep();
	SC_HANDLE hScm = NULL;
	void StopServices();
	void SendStopSignals(std::wstring pwService);
	std::vector<std::wstring> vecServices;
	DWORD  GetServiceState(std::wstring sName);
	void KillService(std::wstring sName);
	void StartServices();
	void OnResume();
	std::wstring ExecOnSleep = L"";
	std::wstring ExecOnWake = L"";
};