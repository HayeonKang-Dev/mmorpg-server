#include "IocpCore.h"

#include <iostream>
#include <iso646.h>
#include <ostream>

#include "Session.h"
#include "SessionManager.h"

IocpCore* IocpCore::s_instance = nullptr;

IocpCore::IocpCore()
{
	s_instance = this;
	// 1. IOCP 커널 객체 ?�성
	m_iocpHandle = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (m_iocpHandle == INVALID_HANDLE_VALUE)
	{
		// ?�러 처리
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
	HANDLE h = ::CreateIoCompletionPort((HANDLE)socket, m_iocpHandle, completionKey, 0);
	return (h != NULL);
}

bool IocpCore::Dispatch(uint32_t timeoutMs)
{
	DWORD bytesTransferred = 0;
	ULONG_PTR completionKey = 0;
	LPOVERLAPPED overlapped = nullptr;

	if (::GetQueuedCompletionStatus(m_iocpHandle, &bytesTransferred,
		&completionKey, &overlapped, timeoutMs))
	{
		if (completionKey == 0)
		{
			Session* session = CONTAINING_RECORD(overlapped, Session, m_acceptOverlapped);

	
			session->SetState(PlayerState::CONNECTED);
			session->UpdateLastTick();

			session->OnAccept(m_listener->GetListenSocket());

			if (m_listener)
			{
				Session* newSession = SessionManager::Get()->Acquire();
				if (newSession)
				{
					m_listener->RegisterAccept(newSession, this);
				}
			}

			return true;
		}

		Session* session = reinterpret_cast<Session*>(completionKey);
		if (session == nullptr) return true;

		if (bytesTransferred > 0)
		{
			if (overlapped == session->GetRecvOverlapped())
			{
				session->OnRecv(bytesTransferred);
			}
			else if (overlapped == session->GetSendOverlapped())
			{
				session->OnSend(bytesTransferred);
			}

		}
		else
		{
			session->Clear();

			// AcceptEx 
			if (m_listener)
			{
				m_listener->RegisterAccept(session, this);
			}
		}

		return true;
	}
	else
	{
		// GQCS 
		if (overlapped != nullptr)
		{
			Session* session = nullptr;
			if (completionKey != 0)
				session = reinterpret_cast<Session*>(completionKey);
			else
				session = CONTAINING_RECORD(overlapped, Session, m_acceptOverlapped);
			if (session == nullptr) return true;
			session->Clear();

			// AcceptEx
			if (m_listener)
			{
				m_listener->RegisterAccept(session, this);
			}
		}
	}

	return true;
}

void IocpCore::HandleSessionDisconnect(Session* session)
{
	session->Clear();
	if (m_listener)
	{
		m_listener->RegisterAccept(session, this);
	}
}
