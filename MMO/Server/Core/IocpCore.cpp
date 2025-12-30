#include "IocpCore.h"

IocpCore::IocpCore()
{
	// 1. IOCP 커널 객체 생성
	m_iocpHandle = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (m_iocpHandle == INVALID_HANDLE_VALUE)
	{
		// 에러 처리 
	}
}

IocpCore::~IocpCore()
{
	if (m_iocpHandle != INVALID_HANDLE_VALUE)
	{
		::CloseHandle(m_iocpHandle);
		m_iocpHandle = INVALID_HANDLE_VALUE;
	}
}

bool IocpCore::Register(SOCKET socket, ULONG_PTR completionKey)
{
	// 2. 기존 IOCP 핸들에 소켓 연결
	// 세 번째 인자인 completionKey는 나중에 완료통지 받을 때 어떤 세션인지 구분하는 용도
	HANDLE h = ::CreateIoCompletionPort((HANDLE)socket, m_iocpHandle, completionKey, 0); 
	return (h != NULL); 
}

bool IocpCore::Dispatch(uint32_t timeoutMs)
{
	DWORD bytesTransferred = 0;
	ULONG_PTR completionkey = 0;
	LPOVERLAPPED overlapped = nullptr;

	// 3. 운영체제가 완료 보고서 던질 때 까지 대기
	if (::GetQueuedCompletionStatus(m_iocpHandle, &bytesTransferred,
		&completionkey, &overlapped, timeoutMs))
	{
		// [성공]
		// 나중에 여기서 overlapped를 이용해 어떤 세션의 어떤 작업(recv/sedn)인지 구분
		return true; 
	}
	else
	{
		// [실패 or 타임아웃]
		int errCode = ::WSAGetLastError();
		if (errCode == WAIT_TIMEOUT) return false;

		// 에러 발생 처리
		return false; 
	}
}



