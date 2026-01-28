#include "IocpCore.h"

#include <iostream>
#include <iso646.h>
#include <ostream>

#include "Session.h"
#include "SessionManager.h"

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
	// 2. 해당 IOCP 핸들에 소켓 등록
	// 세 번째 인자인 completionKey는 나중에 완료통지 받을 때 어떤 세션인지 구분하는 용도
	HANDLE h = ::CreateIoCompletionPort((HANDLE)socket, m_iocpHandle, completionKey, 0);
	return (h != NULL);
}

bool IocpCore::Dispatch(uint32_t timeoutMs)
{
	DWORD bytesTransferred = 0;
	ULONG_PTR completionKey = 0;
	LPOVERLAPPED overlapped = nullptr;

	// 1. 운영체제가 완료 신호를 줄때까지 대기
	if (::GetQueuedCompletionStatus(m_iocpHandle, &bytesTransferred,
		&completionKey, &overlapped, timeoutMs))
	{
		// [케이스 A] 리슨 소켓에서 연결(Accept)이 완료된 경우
		if (completionKey == 0)
		{
			// overlapped 주소를 이용해 Session 객체의 주소 탐색
			Session* session = CONTAINING_RECORD(overlapped, Session, m_acceptOverlapped);

			std::cout << "Accept Complete! Ready to receive data." << std::endl;

			// [수정] 연결 완료 시 상태를 CONNECTED로 설정 (CCU 카운트용)
			session->SetState(PlayerState::CONNECTED);
			session->UpdateLastTick();

			// 연결이 되었으니 데이터를 받을 수 있도록 준비
			session->RegisterRecv();

			// 클라이언트 연결 종료 시에만 해당 세션을 반환하고 다시 AcceptEx 걸기

			return true;
		}

		// [케이스 B] 일반 세션에서 데이터(Recv/Send)가 온 경우
		Session* session = reinterpret_cast<Session*>(completionKey);
		if (session == nullptr) return true;

		if (bytesTransferred > 0)
		{
			if (overlapped == session->GetRecvOverlapped())
			{
				// 데이터가 도착 -> 로깅 후 패킷 처리
				std::cout << "Data Recv Complete! Bytes: " << bytesTransferred << std::endl;
				session->OnRecv(bytesTransferred);
			}
			else if (overlapped == session->GetSendOverlapped())
			{
				std::cout << "Data Send Complete! Bytes: " << bytesTransferred << std::endl;
				session->OnSend(bytesTransferred);
			}

		}
		else
		{
			// [연결 종료] 클라이언트가 연결을 끊거나(0 byte), 에러 발생
			std::cout << "Client Disconnected. Releasing Session..." << std::endl;

			// [수정] Release() 내부에서 Clear()를 호출하므로 여기서 중복 호출 제거
			// SessionManager::Release()가 Clear() + 풀 반환을 모두 처리함
			SessionManager::Get()->Release(session);

			// [수정] 세션이 반환되었으므로 다시 AcceptEx를 걸어줌
			// 같은 세션을 재사용하므로 새로 Acquire할 필요 없음
			if (m_listener)
			{
				m_listener->RegisterAccept(session, this);
			}
		}

		return true;
	}
	else
	{
		// GQCS 자체가 실패
		if (overlapped != nullptr)
		{
			Session* session = reinterpret_cast<Session*>(completionKey);
			// [수정] Release() 내부에서 Clear()를 호출하므로 중복 제거
			SessionManager::Get()->Release(session);

			// AcceptEx 슬롯 복구
			if (m_listener)
			{
				m_listener->RegisterAccept(session, this);
			}
		}
	}

	return true;
}
