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

	// 실제 할당된 버퍼 용량 (풀 반환 여부 판단용)
	int32_t Capacity() const { return (int32_t)m_buffer.size(); }

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
			m_pool.push_back(new SendBuffer(8192)); 
		}
	}

	

	SendBuffer* Open(int32_t size)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		if (m_pool.empty())
		{
			// 풀 소진 시: 실제 패킷 크기만큼만 할당 (8192 고정 → 낭비 제거)
			return new SendBuffer(size);
		}
		SendBuffer* buffer = m_pool.back();
		m_pool.pop_back();
		return buffer;  
	}

	void Release(SendBuffer* buffer)
	{
		std::lock_guard<std::mutex> lock(m_lock);

		// 표준 크기(8192) 버퍼만 풀에 반환
		// 소형 overflow 버퍼를 풀에 넣으면 나중에 더 큰 패킷이 재사용 시 버퍼 오버플로우 위험
		if (buffer->Capacity() < 8192 || m_pool.size() >= 10000)
		{
			delete buffer;
			return;
		}
		m_pool.push_back(buffer);
	}

private:
	std::mutex m_lock;
	std::vector<SendBuffer*> m_pool; 
};