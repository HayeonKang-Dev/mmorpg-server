#pragma once
#include "Session.h"
#include <PacketHandler.h>
#include <stack>

#include "SessionManager.h"
#include "World.h"
#include "../Observability/ServerMonitor.h"

struct Job
{
	Session* session;
	PacketHeader header;
	std::vector<char> data; 
};

class JobPool
{
public:
	static Job* Pop()
	{
		std::lock_guard<std::mutex> lock(m_poolMutex);
		if (m_pool.empty())
		{
			return new Job(); 
		}
		Job* job = m_pool.top();
		m_pool.pop();
		return job; 
	}

	static void Push(Job* job)
	{
		job->session = nullptr;
		job->data.clear();

		std::lock_guard<std::mutex> lock(m_poolMutex);
		m_pool.push(job); 
	}

private:
	static std::stack<Job*> m_pool;
	static std::mutex m_poolMutex; 
};

class LogicManager
{
public:
	static LogicManager* Get()
	{
		static LogicManager instance;
		return &instance;
	}

	// 모니터링용 접근자
	ServerMonitor* GetMonitor() { return &m_monitor; }

	// IO 스레드에서 호출하는 함수
	void PushJob(Job* job)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		jobs.push(job); // 복사없이 포인터 전달
		cv.notify_one();
	}

	// ���� ������ �ϳ��� ȣ��
	void Update()
	{
		while (1)
		{
			std::vector<Job*> currJobs;
			{
				std::unique_lock<std::mutex> lock(m_mutex);

				cv.wait_for(lock, std::chrono::milliseconds(100), [this]
					{
						return !jobs.empty() || m_stop;
					});

				if (m_stop && jobs.empty()) break;

				while (!jobs.empty())
				{
					currJobs.push_back(jobs.front());
					jobs.pop(); 
				}
			}

			for (auto* job : currJobs)
			{
				PacketHandler::HandlePacket(job->session, &job->header, job->data.data());
				JobPool::Push(job);
			}

			World::Get()->Update();

			// [모니터링] 1초마다 성능 로그 기록
			// [수정] IsConnected()는 AcceptEx 대기 세션도 true를 반환하므로,
			// 실제 연결된 세션만 카운트하기 위해 PlayerState를 확인
			int32_t ccu = 0;
			for (Session* s : SessionManager::Get()->GetSessions())
			{
				// NONE이 아닌 상태 = 실제 클라이언트가 연결된 세션
				if (s->GetState() != PlayerState::NONE) ccu++;
			}

			// [AOI 로깅] 1초마다 플레이어별 시야 정보 기록
			World::Get()->LogAllPlayersAOI();

			m_monitor.Update(ccu);
		}
		
	}

	void Shutdown()
	{
		m_stop = true;
		cv.notify_all();
	}

	void CheckSessionTimeout()
	{
		uint64_t now = GetTickCount64();
		const uint64_t timeoutLimit = 15000; // 15초

		std::vector<Session*> sessions = SessionManager::Get()->GetSessions();

		for (Session* session : sessions)
		{
			// [수정] 실제 연결된 세션만 체크 (NONE = 대기 중인 세션)
			if (session->GetState() == PlayerState::NONE) continue;

			if (now - session->GetLastTick() > timeoutLimit)
			{
				std::cout << "[Timeout] Kicking inactive Player: " << session->GetPlayerId() << std::endl;

				SessionManager::Get()->Release(session);
			}
		}
	}

private:
	std::queue<Job*> jobs;
	std::mutex m_mutex;
	std::condition_variable cv;

	bool m_stop = false;

	ServerMonitor m_monitor;  // 성능 모니터링
};

