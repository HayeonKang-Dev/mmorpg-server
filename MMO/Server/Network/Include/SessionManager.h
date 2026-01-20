#pragma once
#include <vector>
#include <queue>
#include <mutex>

#include "Session.h"

class Session;

class SessionManager
{
public:
	// 서버 시작 시 한번만 호출하여 세션을 미리 생성 (생성자 대신) 
	void Init(int maxSessionCount);

	static SessionManager* Get()
	{
		static SessionManager instance;
		return &instance; 
	}
	

	// 비어있는 세션을 하나 빌려줌 (Accept 시 호출)
	Session* Acquire();

	// 사용이 끝난 세션 다시 반남 (Disconnect 시 호출)
	void Release(Session* session);

	std::vector<Session*> GetSessions()
	{
		std::lock_guard<std::mutex> lock(m_mutex); 
		return std::vector<Session*>(m_sessions.begin(), m_sessions.end()); 
	}
private:
	SessionManager() {}; 
	~SessionManager();
	std::vector<Session*> m_sessions;		// 모든 세션 객체 보관용
	std::queue<Session*> m_freeSessions;	// 현재 빌려줄 수 있는 세션들
	std::mutex m_mutex;						// 여러 스레드에서 접근하므로 동기화 필요
};	

