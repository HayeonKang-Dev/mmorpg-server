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


	// 1. ���� �Ŵ��� �ʱ�ȭ (100�� Ǯ��)
	SessionManager::Get()->Init(100);

	// SendBuffer Ǯ �ʱ�ȭ 
	SendBufferManager::Get()->Init(200);

	// 2. DB �Ŵ��� �ʱ�ȭ
	if (DBManager::Get()->Init(2, "tcp://127.0.0.1:3306", "root", "sotptkd", "mmo_db"))
	{
		std::cout << "DB ������ Ǯ ���� ����" << std::endl;
	}

	// 3. IOCP �ھ� �� ������ ����
	IocpCore iocp;
	Listener listener;
	iocp.SetListener(&listener);

	if (listener.StartListen(8001, &iocp) == false)
	{
		std::cout << "Listen ����!" << std::endl;
		return 0;
	}

	// 4. �̸� AcceptEx 10�� �ɾ�α�
	for (int i=0; i<10; i++)
	{
		Session* session = SessionManager::Get()->Acquire();

		// iocp�� ���� �Ѱܼ� Listener ���ο��� ���� ����-���-AcceptEx �ѹ��� �Ͼ�� �� 
		listener.RegisterAccept(session, &iocp); 
	}

	// 5. ��Ŀ ������ 4�� ����
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
