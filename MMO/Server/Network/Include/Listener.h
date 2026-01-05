#pragma once
#include <WinSock2.h>
#include <MSWSock.h>
#include <vector>

class IocpCore;

class Listener
{
public:
	Listener();
	~Listener();

	// 서버를 시작하고 포트를 바인딩
	bool StartListen(int port, IocpCore* iocp, int backlog = 100);

	// AcceptEx 비동기 예약 (세션을 인자로 받음)
	bool RegisterAccept(class Session* session, IocpCore* iocp);

	SOCKET GetListenSocket() { return m_listenSocket; }

private:
	SOCKET m_listenSocket = INVALID_SOCKET;
	LPFN_ACCEPTEX m_fpAcceptEx = nullptr; // AcceptEx 함수 포인터 주소
};

