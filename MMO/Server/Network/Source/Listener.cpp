#include "Listener.h"

#include "IocpCore.h"
#include "Session.h"

Listener::Listener() : m_listenSocket(INVALID_SOCKET), m_fpAcceptEx(nullptr){
	
}
Listener::~Listener()
{
	if (m_listenSocket != INVALID_SOCKET)
	{
		::closesocket(m_listenSocket);
		m_listenSocket = INVALID_SOCKET;
	}
	
}



bool Listener::StartListen(int port, IocpCore* iocp, int backlog)
{
	m_listenSocket = ::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (m_listenSocket == INVALID_SOCKET) return false;

	sockaddr_in serverAddr;
	::ZeroMemory(&serverAddr, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = ::htonl(ADDR_ANY);
	serverAddr.sin_port = ::htons(port);

	if (::bind(m_listenSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) return false;

	if (::listen(m_listenSocket, backlog) == SOCKET_ERROR) return false;

	
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
	SOCKET clientSock = ::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (clientSock == INVALID_SOCKET) return false;

	session->SetSocket(clientSock);

	iocp->Register(clientSock, (ULONG_PTR)session); 

	DWORD bytesReceived = 0;
	if (FALSE == m_fpAcceptEx(m_listenSocket, clientSock,
		session->GetAcceptBuffer(), 0, 
		sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, 
		&bytesReceived, (LPOVERLAPPED)session->GetAcceptOverlapped()))
	{
		if (::WSAGetLastError() != WSA_IO_PENDING) return false; 
	}
	return true; 
}

