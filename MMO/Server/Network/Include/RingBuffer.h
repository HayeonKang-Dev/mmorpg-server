#pragma once
#include <vector>


class RingBuffer {
public:
	RingBuffer(int capacity = 8192);
	~RingBuffer();

	// 데이터를 버퍼에 쓰기 (recv)
	bool Write(const char* data, int len);

	// 데이터 읽어오기 (로직 추출용)
	bool Read(char* dest, int len);

	// 데이터 위치 옮기지 않고 확인만 하기 (헤더 확인용)
	bool Peek(char* dest, int len);
	void Remove(int len);

	// 사용한 만큼 위치 이동
	void Comsume(int len);
	void MoveWritePos(int len); 

	int GetUseSize();		// 현재 쌓인 데이터 양
	int GetFreeSize();	// 남은 공간

	void Clear();
	char* GetWriteBufferPtr();
	int GetContinuousFreeSize();
	int GetContinuousUsedSize(); 

private:
	std::vector<char> m_buffer;
	int m_capacity;
	int m_readPos = 0;
	int m_writePos = 0;
};

