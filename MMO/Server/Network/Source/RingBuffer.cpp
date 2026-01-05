#include "RingBuffer.h"

RingBuffer::RingBuffer(int capacity) {

	// 1. 멤버 변수에 전달받은 크기 저장
	// // (Full과 Empty를 구분하기 위해 한 칸 비워두기)
	m_capacity = capacity + 1;

	// 2. 저장한 크기만큼 할당 
	m_buffer.resize(m_capacity); 
}

RingBuffer::~RingBuffer() {
}


void RingBuffer::MoveWritePos(int len)
{
	// 쓴 양(len)만큼 writePos를 이동시키되, 버퍼 크기를 넘어가면 앞으로 순환(%)시킴
	// (pos + len) % capacity
	m_writePos = (m_writePos + len) % m_capacity;
}

// 현재 버퍼에 쌓여있는 데이터 양 계산
int RingBuffer::GetUseSize() {
	if (m_writePos >= m_readPos) {
		return m_writePos - m_readPos; 
	}
	// writePos가 앞으로 돌아간 경우 
	return (m_capacity - m_readPos) + m_writePos; 
}

int RingBuffer::GetFreeSize()
{
	// 전체 크기에서 사용 중인 크기 빼고,
	// 여유분 1바이트 더 뺀 값을 반환
	return m_capacity - GetUseSize() - 1; 
}

void RingBuffer::Clear()
{
	// 읽기/쓰기 위치를 시작 지점으로 리셋
	m_readPos = 0;
	m_writePos = 0;

	// 만약 사용 중인 크기를 별도의 변수로 관리한다면 여기서 0으로 초기화
	
}

char* RingBuffer::GetWriteBufferPtr()
{
	return &m_buffer[m_writePos]; 
}

int RingBuffer::GetContinuousFreeSize()
{
	// 만약 writePos가 readPos보다 뒤에 있다면 (또는 같으면)
	// 물리적 끝(m_capacity)까지가 연속된 빈 공간
	if (m_writePos >= m_readPos)
	{
		return m_capacity - m_writePos; 
	}

	// writePos가 한 바퀴 돌아와서 readPos보다 앞에 있다면
	// readPos 직전까지만 채울 수 있음
	else
	{
		return m_readPos - m_writePos - 1; 
	}
}

int RingBuffer::GetContinuousUsedSize()
{
	// readPos가 writePos보다 뒤에 있다면, 배열 끝까지만 연속된 데이터임
	if (m_readPos > m_writePos) 
		return m_capacity - m_readPos;

	// readPos가 앞에 있다면 writePos 까지만 연속된 데이터
	return m_writePos - m_readPos;
}

bool RingBuffer::Write(const char* data, int len)
{
	if (GetFreeSize() < len) return false;

	// 1. 현재 writePos부터 배열의 끝까지 남은 공간 계산
	int backFreeSize = m_capacity - m_writePos;

	// 2. 뒤에 한번에 쓸 수 있는 양 계산
	int writeLen = std::min(len, backFreeSize);
	memcpy(&m_buffer[m_writePos], data, writeLen);

	// 3. 배열 끝을 넘어서 남은 데이터가 있다면 맨 앞으로 돌아가서 복사
	if (len > writeLen)
	{
		memcpy(&m_buffer[0], data + writeLen, len - writeLen); 
	}

	// 4. writePos 갱신 (나머지 연산으로 순환)
	m_writePos = (m_writePos + len) % m_capacity;
	return true; 
}

bool RingBuffer::Read(char* dest, int len)
{
	if (GetUseSize() < len) return false;

	// 1. 먼저 Peek으로 데이터만 복사해옴
	if (Peek(dest, len) == false) return false;

	// 2. 실제로 읽었으니 readPos 이동
	Remove(len);
	return true; 
}

bool RingBuffer::Peek(char* dest, int len)
{
	if (GetUseSize() < len) return false;

	// 1. 현재 readPos부터 배열 끝까지의 데이터 양 계산
	int backUseSize = m_capacity - m_readPos;

	// 2. 한 번에 읽을 수 있는 양 만큼 복사
	int readLen = std::min(len, backUseSize);
	memcpy(dest, &m_buffer[m_readPos], readLen);

	// 3. 배열 끝에 걸려 못 읽은 데이터가 있다면 맨 앞으로 가서 복사
	if (len > readLen)
	{
		memcpy(dest + readLen, &m_buffer[0], len - readLen); 
	}

	return true; 
}

void RingBuffer::Remove(int len)
{
	// 실제 데이ㅣ터 지우는 것 아니라 읽기 포인터만 이동시킴
	m_readPos = (m_readPos + len) % m_capacity; 
}




