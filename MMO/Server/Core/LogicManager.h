#pragma once
#include "Session.h"
#include <PacketHandler.h>
#include <stack>

#include "World.h"

struct Job
{
	Session* session;
	PacketHeader header;
	std::vector<char> data; // 패킷 복사본 
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

	// IO 스레드들이 호출하는 함수
	void PushJob(Job* job)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		jobs.push(job); // 불필요한 복사 방지 
		cv.notify_one(); 
	}

	// 로직 스레드 하나가 호출
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

			for (auto* job: currJobs)
			{
				PacketHandler::HandlePacket(job->session, &job->header, job->data.data());
				JobPool::Push(job); 
			}

			World::Get()->Update(); 
		}
		
	}

	void Shutdown()
	{
		m_stop = true;
		cv.notify_all();
	}

private:
	std::queue<Job*> jobs;
	std::mutex m_mutex;
	std::condition_variable cv;

	bool m_stop = false; 
};

