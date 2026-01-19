#pragma once
#include <cstdint>
#include <vector>
#include <mutex>


class SendBuffer
{
public:
	SendBuffer(int32_t size) { m_buffer.resize(size); }

	char* Buffer() { return m_buffer.data();  }

	int32_t Size() { return m_size; }

	void Close(int32_t size) { m_size = size;  }


private:
	std::vector<char> m_buffer;
	int32_t m_size = 0; 
};

class SendBufferManager
{
public:
	static SendBufferManager* Get() { static SendBufferManager instance; return &instance; }

	void Init(int32_t count)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		for (int i=0; i<count; i++)
		{
			// 패킷 최대 사이즈가 4096이니까 8192로 여유 있게 할당
			m_pool.push_back(new SendBuffer(8192)); 
		}
	}

	

	SendBuffer* Open(int32_t size)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		if (m_pool.empty())
		{
			// 풀이 모자라면 이때만 새로 생성 (런타임 할당 최소화) 
			return new SendBuffer(8192); 
		}
		SendBuffer* buffer = m_pool.back();
		m_pool.pop_back();
		return buffer;  
	}

	void Release(SendBuffer* buffer)
	{
		std::lock_guard<std::mutex> lock(m_lock);

		// 다 썼으면 지우지 말고 다시 풀에 넣기 
		m_pool.push_back(buffer); 
	}

private:
	std::mutex m_lock;
	std::vector<SendBuffer*> m_pool; 
};