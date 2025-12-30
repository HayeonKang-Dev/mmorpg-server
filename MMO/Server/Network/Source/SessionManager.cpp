#include "SessionManager.h"
#include "Session.h"

SessionManager::SessionManager(int maxSessionCount)
{
	m_sessions.reserve(maxSessionCount);

	for (int i=0 ;i<maxSessionCount; i++)
	{
		Session* session = new Session();
		m_sessions.push_back(session);
		m_freeSessions.push(session); 
	}
}

SessionManager::~SessionManager()
{
	for (Session* session : m_sessions)
	{
		delete session;
	}
}

Session* SessionManager::Pop()
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (m_freeSessions.empty())
	{
		return nullptr; // 혹은 추가로 생성하는 로직
	}

	Session* session = m_freeSessions.front();
	m_freeSessions.pop();
	return session; 
}

void SessionManager::Push(Session* session)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	// 세션 상태 초기화 (소켓 닫기, 버퍼 비우기 등)
	session->Clear(); // Session 클래스에 초기화 함수 만드는 것이 좋음

	m_freeSessions.push(session); 
}


