#include "Listener.h"

#include "IocpCore.h"
#include "Session.h"

Listener::Listener() : m_listenSocket(INVALID_SOCKET), m_fpAcceptEx(nullptr){
	// 초기화 리스틀를 통해 멤버 변수를 안전하게 초기화
}
Listener::~Listener()
{
	// 서버 종료 시 리슨 소켓 닫아 OS 자원 반환
	if (m_listenSocket != INVALID_SOCKET)
	{
		::closesocket(m_listenSocket);
		m_listenSocket = INVALID_SOCKET;
	}
	
}



bool Listener::StartListen(int port, IocpCore* iocp, int backlog)
{
	// 1. 소켓 생성 (Overlapped 속성 필수)
	m_listenSocket = ::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (m_listenSocket == INVALID_SOCKET) return false;

	// 2. 주소 설정 및 Bind
	sockaddr_in serverAddr;
	::ZeroMemory(&serverAddr, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = ::htonl(ADDR_ANY);
	serverAddr.sin_port = ::htons(port);

	if (::bind(m_listenSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) return false;

	// 3. Listen 상태 진입
	if (::listen(m_listenSocket, 0 /*backlog*/) == SOCKET_ERROR) return false;

	// 4. [중요] AcceptEx 함수 주소 로드
	GUID guidAcceptEx = WSAID_ACCEPTEX;
	DWORD bytes = 0;
	if (::WSAIoctl(m_listenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guidAcceptEx, sizeof(guidAcceptEx), &m_fpAcceptEx, sizeof(m_fpAcceptEx), 
		&bytes, NULL, NULL) == SOCKET_ERROR)
	{
		return false; 
	}

	if (iocp->Register((SOCKET)m_listenSocket, 0) == false) return false; 
	return true; 
}

bool Listener::RegisterAccept(class Session* session, IocpCore* iocp)
{
	// 1. 클라이언트 소켓 생성
	SOCKET clientSock = ::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (clientSock == INVALID_SOCKET) return false;

	session->SetSocket(clientSock);

	// OS에게 " 이 소켓에서 일어나는 모든 일은 iocp로 보고해줘" 등록
	iocp->Register(clientSock, (ULONG_PTR)session); 

	// 2. AcceptEx 호출
	DWORD bytesReceived = 0;
	// session->GetAcceptBuffer() : 수신 대기용 임시 버퍼 (보통 sockaddr_in 2개 크기)
	// session->GetOverlapped() : Session이 가진 WSAOVERLAPPED 주소
	if (FALSE == m_fpAcceptEx(m_listenSocket, clientSock,
		session->GetAcceptBuffer(), 0, 
		sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, 
		&bytesReceived, (LPOVERLAPPED)session->GetAcceptOverlapped()))
	{
		if (::WSAGetLastError() != WSA_IO_PENDING) return false; 
	}
	return true; 
}

