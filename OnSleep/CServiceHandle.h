#pragma once
class CServiceHandle {
public:
	CServiceHandle(SC_HANDLE hnd) {
		m_scHandle = hnd;
	}
	SC_HANDLE operator = (const SC_HANDLE hnd) {
		m_scHandle = hnd;
		return m_scHandle;
	}
	operator SC_HANDLE() {
		return m_scHandle;
	}
	bool empty() {
		return NULL == m_scHandle;
	}
	~CServiceHandle() {
		if (NULL != m_scHandle)
			CloseServiceHandle(m_scHandle);
	}
private:
	SC_HANDLE m_scHandle = NULL;
};