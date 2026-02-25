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
		if (m_pool.size() >= 1000)
		{
			delete job;
			return;
		}
		m_pool.push(job);
	}

private:
	static std::stack<Job*> m_pool;
	static std::mutex m_poolMutex; 
};

class LogicManager
{
public:
	static const int THREAD_COUNT = 4;

	static LogicManager* Get()
	{
		static LogicManager instance;
		return &instance;
	}

	ServerMonitor* GetMonitor() { return &m_monitor; }

	// Route job to fixed thread by session pointer hash (affinity)
	// Same session -> same thread -> no per-session race condition
	void PushJob(Job* job)
	{
		int idx = (int)(((uintptr_t)job->session >> 6) % THREAD_COUNT);
		std::lock_guard<std::mutex> lock(m_mutex[idx]);
		m_jobs[idx].push(job);
		m_cv[idx].notify_one();
	}

	// Each logic thread calls Update(threadIdx)
	void Update(int threadIdx)
	{
		while (1)
		{
			std::vector<Job*> currJobs;
			{
				std::unique_lock<std::mutex> lock(m_mutex[threadIdx]);
				m_cv[threadIdx].wait_for(lock, std::chrono::milliseconds(100), [this, threadIdx]
					{
						return !m_jobs[threadIdx].empty() || m_stop;
					});

				if (m_stop && m_jobs[threadIdx].empty()) break;

				while (!m_jobs[threadIdx].empty())
				{
					currJobs.push_back(m_jobs[threadIdx].front());
					m_jobs[threadIdx].pop();
				}
			}

			for (auto* job : currJobs)
			{
				PacketHandler::HandlePacket(job->session, &job->header, job->data.data());
				JobPool::Push(job);
				m_monitor.AddLogicCount();
			}

			// Only thread 0 runs periodic tasks
			if (threadIdx == 0)
			{
				World::Get()->Update();

				int32_t ccu = 0;
				for (Session* s : SessionManager::Get()->GetSessions())
				{
					if (s->GetState() != PlayerState::NONE) ccu++;
				}

				{
					static uint64_t lastAoiTick = 0;
					uint64_t now = GetTickCount64();
					if (now - lastAoiTick >= 1000)
					{
						lastAoiTick = now;
						World::Get()->LogAllPlayersAOI();
						CheckSessionTimeout();
					}
				}

				m_monitor.Update(ccu);
			}
		}
	}

	void Shutdown()
	{
		m_stop = true;
		for (int i = 0; i < THREAD_COUNT; i++)
			m_cv[i].notify_all();
	}

	void CheckSessionTimeout()
	{
		uint64_t now = GetTickCount64();
		const uint64_t lobbyGameTimeout = 30000; // LOBBY/GAME: 30s
		const uint64_t connectTimeout   = 10000; // CONNECTED: 10s

		std::vector<Session*> sessions = SessionManager::Get()->GetSessions();

		for (Session* session : sessions)
		{
			PlayerState st = session->GetState();
			if (st == PlayerState::NONE) continue;

			uint64_t limit = (st == PlayerState::CONNECTED) ? connectTimeout : lobbyGameTimeout;

			if (now - session->GetLastTick() > limit)
			{
				std::cout << "[Timeout] Kicking Player: " << session->GetPlayerId()
					<< " state=" << (int)st << std::endl;

				SOCKET sock = session->GetSocket();
				if (sock != INVALID_SOCKET)
				{
					session->SetState(PlayerState::NONE); 
					closesocket(sock);
				}
				else
				{
					std::cout << "[Stuck] INVALID_SOCKET state="
						<< (int)st << " id=" << session->GetPlayerId() << std::endl;
				}
			}
		}
	}

private:
	std::queue<Job*>          m_jobs[THREAD_COUNT];
	std::mutex                m_mutex[THREAD_COUNT];
	std::condition_variable   m_cv[THREAD_COUNT];

	bool m_stop = false;

	ServerMonitor m_monitor;
};

