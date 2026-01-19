#include <iostream>

#include "IocpCore.h"
#include "Listener.h"
#include "SessionManager.h"
#include <thread>
#include <vector>

#include "LogicManager.h"
#include "DB/DBManager.h"

int main()
{
	WSADATA wsaData;
	if (::WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 0;


	// 1. 세션 매니저 초기화 (100개 풀링)
	SessionManager::Get()->Init(100);

	// SendBuffer 풀 초기화 
	SendBufferManager::Get()->Init(200);

	// 2. DB 매니저 초기화
	if (DBManager::Get()->Init(2, "tcp://127.0.0.1:3306", "root", "sotptkd", "mmo_db"))
	{
		std::cout << "DB 스레드 풀 가동 성공" << std::endl;
	}

	// 3. IOCP 코어 및 리스너 생성
	IocpCore iocp;
	Listener listener;
	iocp.SetListener(&listener);

	if (listener.StartListen(8001, &iocp) == false)
	{
		std::cout << "Listen 실패!" << std::endl;
		return 0;
	}

	// 4. 미리 AcceptEx 10개 걸어두기
	for (int i=0; i<10; i++)
	{
		Session* session = SessionManager::Get()->Acquire();

		// iocp를 같이 넘겨서 Listener 내부에서 소켓 생성-등록-AcceptEx 한번에 일어나게 함 
		listener.RegisterAccept(session, &iocp); 
	}

	// 5. 워커 스레드 4개 가동
	std::vector<std::thread> workers;
	for (int i=0; i<4; i++)
	{
		workers.push_back(std::thread([&iocp]()
		{
			while (1)
			{
				iocp.Dispatch();
			}
		})); 
	}

	// 6. Logic Thread 가동
	std::thread logicThread([]() {LogicManager::Get()->Update(); });
	logicThread.detach();

	std::cout << "Server Started on Port 8000..." << std::endl;

	// DB가 연결될 시간 (테스트용)
	std::this_thread::sleep_for(std::chrono::milliseconds(500));

	


	for (auto& t : workers) t.join(); 

	return 0;
}
