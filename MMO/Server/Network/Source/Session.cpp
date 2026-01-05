#include "Session.h"
#include <iostream>
#include <MSWSock.h>

Session::Session() : m_recvBuffer(8192) // 8KB 버퍼 할당 
{
	ZeroMemory(&m_recvOverlapped, sizeof(m_recvOverlapped)); 
}

Session::~Session()
{
	if (m_socket != INVALID_SOCKET) closesocket(m_socket); 
}

// 워커 스레드가 "데이터 수신됨"을 알려줄 떄 호출
void Session::OnRecv(int bytesTransferred)
{
	if (bytesTransferred == 0)
	{
		// 연결 종료 처리
		return; 
	}

	// [중요] 실제로 데이터가 버퍼에 들어왔으므로 WritePos를 이동시킴
	m_recvBuffer.MoveWritePos(bytesTransferred);

	// 이제 쌓인 데이터를 패킷 단위로 조립
	ProcessPackets();

	// 다시 다음 데이터를 받을 준비
	RegisterRecv(); 
}

// 패킷 조립
void Session::ProcessPackets()
{
	while (1)
	{
		int dataSize = m_recvBuffer.GetUseSize();

		// 1. 헤더만큼 데이터가 왔는지 확인
		if (dataSize < sizeof(PacketHeader)) break;

		// 2. 헤더 Peek (읽기 위치 이동 없이 정보만 탈취)
		PacketHeader header;
		m_recvBuffer.Peek((char*)&header, sizeof(PacketHeader));

		// 3. 패킷 본문까지 다 왔는지 확인
		if (dataSize < header.size) break;

		// 4. 패킷이 다 왔으니 진짜로 꺼내기 (Read는 포인터까지 이동시킴)
		std::vector<char> buffer(header.size);
		m_recvBuffer.Read(buffer.data(), header.size);

		// 5. 패킷 처리 로직 (현재는 로그만 출력)
		std::cout << "Packet Received! ID: " << header.id << ", Size: " << header.size << std::endl;

		// 여기에 LogicThread 큐에 넣는 코드가 들어갈 예정 
	}
}

void Session::Clear()
{
	// 1. 소켓 닫기
	if (m_socket != INVALID_SOCKET)
	{
		closesocket(m_socket);
		m_socket = INVALID_SOCKET; 
	}

	// 2. 링버퍼 초기화
	m_recvBuffer.Clear();

	// 3. IOCP 관련 구조체 초기화
	// 다음 AcceptEx나 Recv를 위해 깨끗한 상태로 만들기
	ZeroMemory(&m_recvOverlapped, sizeof(m_recvOverlapped));

	// 4. 기타 세션 정보 (유저 ID, 좌표 등 게임 로직 정보)
	// m_userId = 0;
	// m_isLoggedIn = false;

	std::cout << "Session Cleared and Ready for Reuse." << std::endl;
}

// OS에게 데이터가 들어오면 링버퍼의 빈 공간에 바로 써달라 예약
void Session::PreRecv()
{
	// 1. 링버퍼에서 현재 쓸 수 있는 공간의 주소와 크기 가져옴
	// writePos 위치부터 끝까지 혹은 빈 공간만큼
	int freeSize = m_recvBuffer.GetFreeSize();
	if (freeSize <= 0) return; // 버퍼 꽉 참!

	// 2. WSABUF 설정 (운영체제가 데이터를 직접 채울 바구니)
	WSABUF wsaBuf;
	wsaBuf.buf = m_recvBuffer.GetWriteBufferPtr(); // 현재 writePos의 메모리 주소
	wsaBuf.len = m_recvBuffer.GetContinuousFreeSize(); // 선형적으로 비어있는 공간

	// 3. 비동기 수신 예약 (WSARecv)
	DWORD flags = 0;
	DWORD recvBytes = 0;

	// 핵심: m_recvOverlapped를 넘겨서 완료 알림 받음
	if (SOCKET_ERROR == ::WSARecv(m_socket, &wsaBuf, 1, &recvBytes, &flags, &m_recvOverlapped, nullptr))
	{
		if (::WSAGetLastError() != WSA_IO_PENDING)
		{
			// 진짜 에러 발생 시 처리 
		}
	}
}


// 수신 예약 
void Session::RegisterRecv()
{
	// Overlapped 구조체 초기화
	::ZeroMemory(&m_recvOverlapped, sizeof(m_recvOverlapped));

	WSABUF wsaBuf;
	wsaBuf.buf = m_recvBuffer.GetWriteBufferPtr();
	wsaBuf.len = m_recvBuffer.GetContinuousFreeSize(); // 일단 연속된 공간만큼만 받음

	DWORD flags = 0;
	DWORD bytesReceived = 0;

	// Overlapped 구조체를 Recv용으로 설정해서 던짐
	if (SOCKET_ERROR ==::WSARecv(m_socket, &wsaBuf, 1, &bytesReceived, &flags, &m_recvOverlapped, nullptr))
	{
		int errCode = ::WSAGetLastError();
		if (errCode != WSA_IO_PENDING)
		{
			// 실제 에러 처리 로직 필요 
		}
	}
}

void Session::OnAccept(SOCKET listenSocket)
{
	// 1. AcceptEx로 생성된 소켓에 리슨 소켓의 특성 입히기
	::setsockopt(m_socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&listenSocket, sizeof(listenSocket));

	// 2. [가장 중요] 접속하자마자 첫 번째 수신 예약 걸어주기
	// 클라이언트가 send하면 서버 OS가 받도록 함
	RegisterRecv();

	std::cout << "Client Connected and Ready to Recv!" << std::endl;
}


