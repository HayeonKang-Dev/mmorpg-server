#pragma once
#include "Session.h"
#include <PacketHandler.h>

struct Job
{
	Session* session;
	PacketHeader header;
	std::vector<char> data; // 패킷 복사본 
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
	void PushJob(Job job)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		jobs.push(std::move(job)); // 불필요한 복사 방지 
		cv.notify_one(); 
	}

	// 로직 스레드 하나가 호출
	void Update()
	{
		while (1)
		{
			std::vector<Job> currJobs;
			{
				std::unique_lock<std::mutex> lock(m_mutex);

				// 1. 작업 올 때 까지 대기
				cv.wait(lock, [this] {return !jobs.empty() || m_stop; });
				if (m_stop && jobs.empty()) break;

				// 2. 현재 쌓인 모든 작업 꺼내오기
				// => 락 잡는 횟수를 줄여 성능 상승
				while (!jobs.empty())
				{
					currJobs.push_back(std::move(jobs.front()));
					jobs.pop(); 
				}
			}
			// 3. 락 풀고 작업 일괄 처리 (MPSC Consumer)
			for (auto & job: currJobs)
			{
				PacketHandler::HandlePacket(job.session, &job.header, job.data.data());
			}
		}
		
	}

	void Shutdown()
	{
		m_stop = true;
		cv.notify_all();
	}

private:
	std::queue<Job> jobs;
	std::mutex m_mutex;
	std::condition_variable cv;

	bool m_stop = false; 
};

