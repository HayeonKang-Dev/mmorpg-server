#pragma once
#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <vector>
#include "RingBuffer.h"

struct PacketHeader
{
	uint16_t size;	// 패킷 전체 크기
	uint16_t id;	// 패킷 종류 
};

class Session
{
public:
	Session();
	~Session();

	void SetSocket(SOCKET sock) { m_socket = sock;}
	SOCKET GetSocket() { return m_socket; }

	// 네트워크 엔진(IOCP)에서 호출할 함수들
	void OnRecv(int bytesTransferred);
	void OnSend(int bytesTransferred);

	// 패킷 조립 로직
	void ProcessPackets();

	void Clear();

	void PreRecv(); 

	// Accept 전용 Overlapped 주소 넘겨줌 
	WSAOVERLAPPED* GetAcceptOverlapped() { return &m_acceptOverlapped;  }

	// Recv 전용 Overlapped 주소 넘겨줌 (RegisterRecv에서 사용)
	WSAOVERLAPPED* GetRecvOverlapped() { return &m_recvOverlapped; }

	// Accept 시 주소 정보를 담을 버퍼 (최소 64바이트 이상 권장)
	char* GetAcceptBuffer() { return m_acceptBuffer;  }

	void RegisterRecv();

	void OnAccept(SOCKET listenSocket);

	// IOCP용 구조체
	WSAOVERLAPPED m_acceptOverlapped; // 1번: 접속 대기용
	WSAOVERLAPPED m_recvOverlapped;	  // 2번: 데이터 수신용

private:
	SOCKET m_socket = INVALID_SOCKET;
	RingBuffer m_recvBuffer; // 수신데이터 모아두는 곳

	
	

	char m_acceptBuffer[128];

	int32_t _packetProcessLimit = 10;
};

