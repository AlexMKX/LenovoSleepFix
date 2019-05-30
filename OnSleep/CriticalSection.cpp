#include "CriticalSection.h"


CCriticalSection::CCriticalSection():
	m_bInitialized(false)	
{
    // Needed #define _WIN32_WINNT  0x0403
	if (InitializeCriticalSectionAndSpinCount(&m_sect, 0x80000400) )
	{
		m_bInitialized = true;
	}
}

bool CCriticalSection::Lock()
{
	EnterCriticalSection(&m_sect);
	return m_bInitialized;
}

bool CCriticalSection::Unlock()
{
	LeaveCriticalSection(&m_sect);
	return m_bInitialized;
}

CCriticalSection::~CCriticalSection()
{
	DeleteCriticalSection(&m_sect);
}

