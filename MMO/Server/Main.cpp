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

	// Quick Edit Mode 비활성화 — 콘솔 클릭해도 서버가 멈추지 않음
	HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
	DWORD mode;
	GetConsoleMode(hInput, &mode);
	mode &= ~ENABLE_QUICK_EDIT_MODE;
	SetConsoleMode(hInput, mode);

	SetConsoleOutputCP(949);

	// 1. 세션 매니저 초기화 (최대 CCU  + AcceptEx 여유분 10)
	SessionManager::Get()->Init(1530); 

	// SendBuffer 풀 초기화
	SendBufferManager::Get()->Init(5000); 

	// 2. DB 매니저 초기화
	if (DBManager::Get()->Init(2, "tcp://127.0.0.1:3306", "root", "sotptkd", "mmo_db"))
	{
		std::cout << "DB 커넥션 풀 생성 성공" << std::endl;
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

	// 4. 미리 AcceptEx 20개 걸어놓기(10에서 20으로 증가시킴)
	for (int i=0; i<20; i++)
	{
		Session* session = SessionManager::Get()->Acquire();

		// iocp에 함께 넘겨서 Listener 내부에서 소켓 생성-등록-AcceptEx 한번에 일어나게 함 
		listener.RegisterAccept(session, &iocp); 
	}

	// 5. 워커 스레드 4개 생성
	std::vector<std::thread> workers;
	for (int i=0; i<4; i++)
	{
		workers.push_back(std::thread([&iocp, i]()
		{
			while (1)
			{
				iocp.Dispatch();
				LogicManager::Get()->GetMonitor()->AddWorkerCount(i);
			}
		}));
	}

	// 6. Logic Thread x4 (session affinity-based)
	for (int i = 0; i < LogicManager::THREAD_COUNT; i++)
	{
		std::thread t([i]() { LogicManager::Get()->Update(i); });
		t.detach();
	}

	std::cout << "Server Started on Port 8001..." << std::endl;

	// DB와 연결할 시간 (테스트용)
	std::this_thread::sleep_for(std::chrono::milliseconds(500));

	

	for (auto& t : workers) t.join(); 

	return 0;
}
