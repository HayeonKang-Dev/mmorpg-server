#pragma once
#include "Listener.h"

class Listener;
class Session;

class IocpCore
{
public:
	IocpCore();
	~IocpCore();

	static IocpCore* Get() { return s_instance; }

	// IOCP 핸들 반환
	HANDLE GetHandle() { return m_iocpHandle; }

	// 소켓을 IOCP에 등록
	bool Register(SOCKET socket, ULONG_PTR completionKey = 0);

	// 완료된 작업 확인 (GetQueuedCompletionStatus 호출)
	bool Dispatch(uint32_t timeoutMs = INFINITE);

	void SetListener(Listener* listener) { m_listener = listener; }
	void HandleSessionDisconnect(Session* session);

private:
	static IocpCore* s_instance;
	HANDLE m_iocpHandle = INVALID_HANDLE_VALUE;
	Listener* m_listener = nullptr; 
};

