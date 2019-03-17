
#include <stdio.h>
#include <windows.h>
#include "ServiceInstaller.h"
#include "ServiceBase.h"
#include <boost/program_options.hpp>
#include <iostream>
#include <fcntl.h>
#include <io.h>
#include "WinReg.hpp"

namespace po = boost::program_options;



#define SERVICE_NAME             L"OnSleep Service"
#define SERVICE_DISPLAY_NAME     L"OnSleepService"
#define SERVICE_START_TYPE       SERVICE_AUTO_START
#define SERVICE_DEPENDENCIES     L""
#define SERVICE_ACCOUNT          L"NT AUTHORITY\\LocalSystem"
#define SERVICE_PASSWORD         NULL


int wmain(int argc, wchar_t *argv[])
{
	winreg::RegKey key(HKEY_LOCAL_MACHINE, L"SOFTWARE\\OnSleep");
	
	
	try {
		po::options_description desc("Allowed options");
		desc.add_options()
			("help", "produce help message")
			("install", "installs service")
			("uninstall", "removes service")
			("onsleep", po::wvalue<std::wstring>(), "command to execute on sleep")
			("onwake", po::wvalue<std::wstring>(), "command to execute on wake")
			("nocommands", "clear commands to execute on sleep/wake")
			("commands", "show commands to execute on sleep/wake")

			;

		po::variables_map vm;
		po::store(po::parse_command_line(argc, argv, desc), vm);
		po::notify(vm);

		if (vm.count("help")) {
			desc.print(std::cout);
			return 0;
		}
		_setmode(_fileno(stdout), _O_U16TEXT);
		if (vm.count("onsleep")) {
			std::wstring w = vm["onsleep"].as<std::wstring>();
			key.SetStringValue(L"OnSleep", w);
		}
		if (vm.count("onwake")) {
			std::wstring w = vm["onwake"].as<std::wstring>();
			key.SetStringValue(L"OnWake", w);
		}
		if (vm.count("nocommands")) {
			key.SetStringValue(L"OnSleep", L"");
			key.SetStringValue(L"OnWake", L"");
			key.DeleteValue(L"OnWake");
			key.DeleteValue(L"OnSleep");
		}
		if (vm.count("commands")) {
			auto vals = key.EnumValues();
			std::wcout << "OnSleep=";
			if (std::find(vals.begin(), vals.end(), std::make_pair(std::wstring(L"OnSleep"), REG_SZ)) != vals.end())
				std::wcout << key.GetStringValue(L"OnSleep") << std::endl;
			else std::wcout << L"UNSET" << std::endl;
			std::wcout << "OnWake=";
			if (std::find(vals.begin(), vals.end(), std::make_pair(std::wstring(L"OnWake"), REG_SZ)) != vals.end())
				std::wcout << key.GetStringValue(L"OnWake") << std::endl;
			else std::wcout << L"UNSET" << std::endl;
		}
		if (vm.count("install"))
		{
			InstallService(
				SERVICE_NAME,               // Name of service
				SERVICE_DISPLAY_NAME,       // Name to display
				SERVICE_START_TYPE,         // Service start type
				SERVICE_DEPENDENCIES,       // Dependencies
				NULL,            // Service running account
				SERVICE_PASSWORD            // Password of the account
			);
		}
		if (vm.count("uninstall"))
		{
			UninstallService(SERVICE_NAME);
		}
		if (argc==1)
		{
			CServiceBase service(SERVICE_NAME);
			if (!CServiceBase::Run(service))
			{
				wprintf(L"Service failed to run w/err 0x%08lx\n", GetLastError());
			}
		}
	}
	catch (std::exception e)
	{
		std::cerr << e.what();
	}
	catch (...) {
		std::cerr << "Exception of unknown type!\n";
	}
    return 0;
}