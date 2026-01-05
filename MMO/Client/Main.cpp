#include <WinSock2.h>
#include <iostream>
#include <vector>
#include <WS2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

// 서버와 동일한 헤더
struct PacketHeader
{
	uint16_t size;	// 패킷 전체 크기 (헤더 4 + 데이터 2 = 6)
	uint16_t id;	// 패킷 종류 (1) 
};

int main()
{
	// 1. 윈속 초기화
	WSAData wsaData;
	if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) return 0;

	// 2. 소켓 생성
	SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (clientSocket == INVALID_SOCKET) return 0;

	// 3. 서버 주소 설정 (로컬 서버 포트 8000)
	SOCKADDR_IN serverAddr = {};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(8000);
	if (::inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr) <= 0)
	{
		std::cout << "IP 주소 변환 실패" << std::endl;
		return 0; 
	}

	// 4. 서버 접속
	std::cout << "Connecting to Server..." << std::endl;
	if (connect(clientSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
	{
		std::cout << "Connect Error!" << std::endl;
		return 0; 
	}
	std::cout << "Connected!" << std::endl;

	// 5. 패킷 데이터 구성
	// [Header(4 bytes)] + [Data(2 bytes)] = Total 6 bytes
	char sendBuffer[6];
	PacketHeader* header = (PacketHeader*)sendBuffer;
	header->size = 6;
	header->id = 1;

	// 데이터 부분 (단순 테스트용)
	sendBuffer[4] = 0x11;
	sendBuffer[5] = 0x22;

	// 6. 1초마다 반복 전송
	while (1)
	{
		int result = send(clientSocket, sendBuffer, 6, 0);
		if (result == SOCKET_ERROR)
		{
			std::cout << "Send Error or Disconnected." << std::endl;
			break; 
		}

		std::cout << "Send Packet: ID(1), Size(6)" << std::endl;
		Sleep(1000); 
	}

	closesocket(clientSocket);
	WSACleanup();
	return 0; 
}