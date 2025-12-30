#pragma once
#include <vector>
#include <queue>
#include <mutex>

class Session;

class SessionManager
{
public:
	// 서버 시작 시 최대 세션 개수만큼 미리 생성
	SessionManager(int maxSessionCount);
	~SessionManager();

	// 비어있는 세션을 하나 빌려줌 (Accept 시 호출)
	Session* Pop();

	// 사용이 끝난 세션 다시 반남 (Disconnect 시 호출)
	void Push(Session* session);

private:
	std::vector<Session*> m_sessions;		// 모든 세션 객체 보관용
	std::queue<Session*> m_freeSessions;	// 현재 빌려줄 수 있는 세션들
	std::mutex m_mutex;						// 여러 스레드에서 접근하므로 동기화 필요
};	

