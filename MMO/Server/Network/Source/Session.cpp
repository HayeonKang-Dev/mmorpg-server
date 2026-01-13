#include "Session.h"
#include <iostream>
#include <MSWSock.h>

#include "LogicManager.h"
#include "SessionManager.h"

Session::Session() : m_recvBuffer(8192) // 8KB 버퍼 할당 
{
	ZeroMemory(&m_recvOverlapped, sizeof(m_recvOverlapped)); 
}

Session::~Session()
{
	if (m_socket != INVALID_SOCKET) closesocket(m_socket); 
}

bool Session::Send(char* ptr, int len)
{
	{
		std::lock_guard<std::mutex> lock(m_sendQueueMutex);
		// Backpressure: 보낼 데이터가 너무 많이 쌓여있다면 세션 차단
		// 5000 수치는 서버 사양에 따라 조절 가능, 보통 비정상 상황
		if (m_sendQueue.size() > 5000)
		{
			std::cout << "[Backpressure] SendQueue Overload! Disconnecting..." << std::endl;

			// 즉시 소켓 닫거나, 반환값 보고 외부에서 정리하도록 함
			// 여기선 안전히 false 리턴, 이 세션을 끊는 로직 유도
			return false; 
		}
		// [수정] SendBufferManager에서 버퍼 빌려오기 
		SendBuffer* sendBuffer = SendBufferManager::Get()->Open(len);
		memcpy(sendBuffer->Buffer(), ptr, len);
		sendBuffer->Close(len); // 보낼 크기 확정

		m_sendQueue.push(sendBuffer);
		if (m_isSending) return true;
		m_isSending = true; 

	}

	
	return RegisterSend();
}

// OS에게 이 데이터를 보내달라 예약 ! 
bool Session::RegisterSend()
{
	WSABUF wsaBufs[100];
	int bufCount = 0;

	{
		std::lock_guard<std::mutex> lock(m_sendQueueMutex);
		if (m_sendQueue.empty())
		{
			m_isSending = false;
			return false;
		}

		// [핵심] 큐에 있는 모든 SendBuffer를 긁어모아 Batch 송신 준비
		while (!m_sendQueue.empty() && bufCount < 100)
		{
			SendBuffer* sendBuffer = m_sendQueue.front();
			m_sendQueue.pop();

			// 전송 완료 시 해제하기 위해 '전송 중 리스트'에 보관
			m_sendingList.push_back(sendBuffer);

			wsaBufs[bufCount].buf = sendBuffer->Buffer();
			wsaBufs[bufCount].len = (ULONG)sendBuffer->Size();
			bufCount++;
		}
		::ZeroMemory(&m_sendOverlapped, sizeof(m_sendOverlapped));
	}

	DWORD sendBytes = 0;
	// 이제 한 번에 쏨 (Batch Send)
	if (SOCKET_ERROR == ::WSASend(m_socket, wsaBufs, bufCount, &sendBytes, 0, &m_sendOverlapped, nullptr))
	{
		if (::WSAGetLastError() != WSA_IO_PENDING)
		{
			std::lock_guard<std::mutex> lock(m_sendQueueMutex);
			m_isSending = false;
			return false;
		}
	}
	return true;

}

// 워커 스레드가 "데이터 수신됨"을 알려줄 떄 호출
void Session::OnRecv(int bytesTransferred)
{
	if (bytesTransferred == 0)
	{
		// 연결 종료 처리
		this->Clear();
		SessionManager::Get()->Release(this); 
		return; 
	}

	// [중요] 실제로 데이터가 버퍼에 들어왔으므로 WritePos를 이동시킴
	m_recvBuffer.MoveWritePos(bytesTransferred);

	// 이제 쌓인 데이터를 패킷 단위로 조립
	ProcessPackets();

	// 다시 다음 데이터를 받을 준비
	RegisterRecv(); 
}

void Session::OnSend(int bytesTransferred)
{
	// [수정] 전송이 끝난 버퍼들을 매니저에게 반납
	for (SendBuffer* sendBuffer : m_sendingList)
	{
		SendBufferManager::Get()->Release(sendBuffer);
	}
	m_sendingList.clear();

	{
		std::lock_guard<std::mutex> lock(m_sendQueueMutex);
		if (m_sendQueue.empty())
		{
			m_isSending = false;
			return;
		}
	}

	// 아직 큐에 남은 게 있다면 다시 전송
	RegisterSend();
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

		// 패킷 사이즈 유효성 검사
		if (header.size > 4096 || header.size < sizeof(PacketHeader))
		{
			this->Clear();
			break; 
		}

		// 3. 패킷 본문까지 다 왔는지 확인
		if (dataSize < header.size) break;

		// 헤더를 뺀 실제 데이터 크기 계산
		int payloadSize = header.size - sizeof(PacketHeader);

		// Job 생성 및 데이터 복사
		Job job;
		job.session = this;
		job.header = header;
		job.data.resize(payloadSize);

		// 링버퍼에서 헤더 건너뛰고 본문만 읽기 (or 한꺼번에 읽고 나누기)
		m_recvBuffer.Read(nullptr, sizeof(PacketHeader)); // 헤더는 이미 Peek으로 확인
		m_recvBuffer.Read(job.data.data(), payloadSize); // 본문 복사

		// 로직스레드로 배달~
		LogicManager::Get()->PushJob(std::move(job)); 

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

	// 3. Send Queue 비우기 (남아있는 버퍼 있다면 반납)
	{
		std::lock_guard<std::mutex> lock(m_sendQueueMutex);
		while (!m_sendQueue.empty())
		{
			SendBufferManager::Get()->Release(m_sendQueue.front());
			m_sendQueue.pop(); 
		}
		for (auto* buf : m_sendingList) SendBufferManager::Get()->Release(buf);
		m_sendingList.clear();
		m_isSending = false; 
	}

	// 4. IOCP 관련 구조체 초기화
	// 다음 AcceptEx나 Recv를 위해 깨끗한 상태로 만들기
	ZeroMemory(&m_recvOverlapped, sizeof(m_recvOverlapped));
	ZeroMemory(&m_sendOverlapped, sizeof(m_sendOverlapped));
	ZeroMemory(&m_acceptOverlapped, sizeof(m_acceptOverlapped)); 

	// 4. 기타 세션 정보 (유저 ID, 좌표 등 게임 로직 정보)
	// m_userId = 0;
	// m_isLoggedIn = false;

	std::cout << "Session Cleared and Ready for Reuse." << std::endl;
}




// 수신 예약 
void Session::RegisterRecv()
{
	// 연속된 공간이 너무 작으면 공간 확보를 시도하거나 세션 정리
	if (m_recvBuffer.GetContinuousFreeSize() <= 0)
	{
		// Read/Write Pos가 같앙서 공간이 없는 거라면, FULL
		if (m_recvBuffer.GetFreeSize() < sizeof(PacketHeader))
		{
			std::cout << "Buffer FULL! Disconnecting... " << std::endl;
			this->Clear();
			SessionManager::Get()->Release(this);
			return; 
		}
	}

	// Overlapped 구조체 초기화
	::ZeroMemory(&m_recvOverlapped, sizeof(m_recvOverlapped));

	WSABUF wsaBuf;
	wsaBuf.buf = m_recvBuffer.GetWriteBufferPtr();
	wsaBuf.len = m_recvBuffer.GetContinuousFreeSize(); // 일단 연속된 공간만큼만 받음

	DWORD flags = 0;
	DWORD bytesReceived = 0;

	if (SOCKET_ERROR == ::WSARecv(m_socket, &wsaBuf, 1, &bytesReceived, &flags, &m_recvOverlapped, nullptr))
	{
		if (::WSAGetLastError() != WSA_IO_PENDING)
		{
			this->Clear();
			SessionManager::Get()->Release(this); 
		}
	}

}

void Session::OnAccept(SOCKET listenSocket)
{
	BOOL opt = true;
	// 1. AcceptEx로 생성된 소켓에 리슨 소켓의 특성 입히기
	::setsockopt(m_socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(opt)); 

	// 2. [가장 중요] 접속하자마자 첫 번째 수신 예약 걸어주기
	// 클라이언트가 send하면 서버 OS가 받도록 함
	RegisterRecv();

	std::cout << "Client Connected and Ready to Recv!" << std::endl;
}


