#pragma once
#include <windows.h>

class CCriticalSection
{
public:
	CCriticalSection();
	~CCriticalSection();

	bool Lock();
	bool Unlock();
	CRITICAL_SECTION m_sect;
protected:

	bool m_bInitialized;
};
