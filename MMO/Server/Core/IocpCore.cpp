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
	// 2. 기존 IOCP 핸들에 소켓 연결
	// 세 번째 인자인 completionKey는 나중에 완료통지 받을 때 어떤 세션인지 구분하는 용도
	HANDLE h = ::CreateIoCompletionPort((HANDLE)socket, m_iocpHandle, completionKey, 0); 
	return (h != NULL); 
}

bool IocpCore::Dispatch(uint32_t timeoutMs)
{
	DWORD bytesTransferred = 0;
	ULONG_PTR completionKey = 0;
	LPOVERLAPPED overlapped = nullptr;

	// 1. 운영체제가 완료 보고서 던질 때까지 대기
	if (::GetQueuedCompletionStatus(m_iocpHandle, &bytesTransferred,
		&completionKey, &overlapped, timeoutMs))
	{
		// [케이스 A] 리슨 소켓에서 접속(Accept)이 완료된 경우
		if (completionKey == 0)
		{
			// overlapped 주소를 이용해 Session 객체의 주소를 찾아냅니다.
			// Session 클래스의 m_acceptOverlapped 멤버 변수 위치를 기준으로 역산합니다.
			Session* session = CONTAINING_RECORD(overlapped, Session, m_acceptOverlapped);

			std::cout << "Accept 완료 신호 감지! 이제 데이터를 받을 준비를 합니다." << std::endl;

			// 접속이 되었으니 리슨 소켓의 특성을 클라 소켓에 입히고 (필요 시)
			// 실제로 데이터를 받을 수 있도록 그물을 던집니다.
			session->RegisterRecv();

			// AcceptEx 채워주기
			Session* newSession = SessionManager::Get()->Acquire();
			if (newSession == nullptr)
			{
				std::cout << "[Warning] 세션 풀 고갈! 접속 거부" << std::endl; 
			}
			if (newSession && m_listener)
			{
				m_listener->RegisterAccept(newSession, this); 
			}

			return true;
		}

		// [케이스 B] 일반 세션에서 데이터(Recv/Send)가 온 경우
		Session* session = reinterpret_cast<Session*>(completionKey);
		if (session == nullptr) return true;

		if (bytesTransferred > 0)
		{
			if (overlapped == session->GetRecvOverlapped())
			{
				// 데이터가 들어옴 -> 로그 출력 및 로직 처리
				std::cout << "데이터 수신 완료! 바이트: " << bytesTransferred << std::endl;
				session->OnRecv(bytesTransferred);
			}
			else if (overlapped == session->GetSendOverlapped())
			{
				std::cout << "데이터 송신 완료! 바이트: " << bytesTransferred << std::endl;
				session->OnSend(bytesTransferred); 
			}
			
		}
		else
		{
			// [세션 회수 로직] 클라이언트가 접속을 끊었거나(0 byte), 에러 발생
			std::cout << "Client Disconnected. Releasing Session..." << std::endl;

			// 세션 정리 로직 (session->Close() 등) 호출 필요
			// 1. 세션의 소켓 닫고 내부 버퍼 비우기
			session->Clear(); 

			// 2. 세션 매니저의 freeSessions 큐로 돌려보내기
			SessionManager::Get()->Release(session);

			// 3. (opt) 빈 자리 생겼으니 다시 새로운 접속 받을 수 있게
			// 리스너에 RegisterAccept를 한 번 더 호출해 줄 수도 있음
			if (m_listener)
			{
				Session* newSession = SessionManager::Get()->Acquire();
				if (newSession) m_listener->RegisterAccept(newSession, this); 
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
			session->Clear();
			SessionManager::Get()->Release(session); 
		};
	}
}


