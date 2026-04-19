#include <WinSock2.h>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <WS2tcpip.h>
#include <cstdlib>
#include <ctime>

#include "Protocol.h"


#pragma comment(lib, "ws2_32.lib")

static constexpr int MOVE_RANGE = 30;  // 틱당 최대 이동거리 (±MOVE_RANGE)

int32_t myPlayerId = 0;
std::vector<int32_t> nearbyPlayers;
std::mutex g_listMutex; // nearbyPlayers 보호용 뮤텍스

float myX = 0.0f;
float myY = 0.0f;

SOCKET g_clientSocket = INVALID_SOCKET;

void ReceiveThread(SOCKET sock)
{
    std::vector<char> buffer;
    buffer.reserve(4096);
    char temp[1024];

    while (true)
    {
        // 1. 서버로부터 데이터 수신
        int len = recv(sock, temp, 1024, 0);
        if (len <= 0) {
            std::cout << "Disconnected from Server (ReceiveThread)." << std::endl;
            break;
        }

        buffer.insert(buffer.end(), temp, temp + len);

        // 2. 수신 버퍼에서 패킷 파싱
        while (buffer.size() >= sizeof(PacketHeader))
        {
            PacketHeader* header = (PacketHeader*)buffer.data();
            if (buffer.size() < header->size) break;

            // 패킷 종류별 처리
            if (header->id == PKT_S_LOGIN) {
                S_LOGIN* res = (S_LOGIN*)buffer.data();
                if (res->success) {
                    myPlayerId = res->playerId;
                    std::cout << "Login Success! ID: " << myPlayerId << std::endl;

                    // 로그인 성공 시 게임 입장 요청
                    PacketHeader enterGamePkt;
                    enterGamePkt.size = sizeof(PacketHeader);
                    enterGamePkt.id = PKT_C_ENTER_GAME;
                    send(sock, (char*)&enterGamePkt, sizeof(PacketHeader), 0);
                    std::cout << "Sent C_ENTER_GAME packet" << std::endl;
                }
            }
            else if (header->id == PKT_S_MOVE) {
                // 이동 로그는 부하 테스트 시 출력 생략
                //S_MOVE* res = (S_MOVE*)buffer.data();
                //std::cout << "[Move] Player " << res->playerId << " -> (" << res->x << ", " << res->y << ")" << std::endl;
            }
            else if (header->id == PKT_S_ATTACK) {
                // 공격 로그 생략 (부하 테스트용)
            }
            else if (header->id == PKT_S_DIE) {
                S_DIE* pkt = (S_DIE*)buffer.data();
                if (pkt->playerId == myPlayerId)
                    std::cout << "[DIE] I died!" << std::endl;
                else
                    std::cout << "[DIE] Player " << pkt->playerId << " died." << std::endl;
            }
            else if (header->id == PKT_S_SPAWN) {
                S_SPAWN* pkt = (S_SPAWN*)buffer.data();
                if (pkt->playerId == myPlayerId) {
                    // 서버가 정해준 스폰 위치로 동기화
                    myX = pkt->x;
                    myY = pkt->y;
                    std::cout << "[Notice] My spawn position: (" << myX << ", " << myY << ")" << std::endl;
                }
                else {
                    std::lock_guard<std::mutex> lock(g_listMutex);
                    if (std::find(nearbyPlayers.begin(), nearbyPlayers.end(), pkt->playerId) == nearbyPlayers.end()) {
                        nearbyPlayers.push_back(pkt->playerId);
                        // 스폰 로그 생략 (부하 테스트용)
                    }
                }
            }
            else if (header->id == PKT_S_DESPAWN) {
                // 시야에서 벗어난 플레이어 목록 제거
                std::lock_guard<std::mutex> lock(g_listMutex);
                // nearbyPlayers.erase(...)
            }
            else if (header->id == PKT_S_RESPAWN)
            {
                S_RESPAWN* res = (S_RESPAWN*)buffer.data();
                if (res->playerId == myPlayerId)
                {
                    myX = res->x;
                    myY = res->y;
                }
                else
                {
                    std::lock_guard<std::mutex> lock(g_listMutex);
                    if (std::find(nearbyPlayers.begin(), nearbyPlayers.end(), res->playerId) == nearbyPlayers.end())
                    {
                        nearbyPlayers.push_back(res->playerId);
                    }
                }
            }
            else if (header->id == PKT_S_CHAT)
            {
                S_CHAT* res = (S_CHAT*)buffer.data();
                if (res->playerId == myPlayerId) std::cout << "[ME] : " << res->chat << std::endl;
                else std::cout << "[Player " << res->playerId << "] : " << res->chat << std::endl;
            }

            buffer.erase(buffer.begin(), buffer.begin() + header->size);
        }
    }
}


// 채팅 입력 스레드
void InputThread(SOCKET sock)
{
	while (true)
	{
        char msg[128];
        std::cin.getline(msg, 128);

        if (strlen(msg) > 0)
        {
            C_CHAT chatPkt;
            chatPkt.header.id = PKT_C_CHAT;
            chatPkt.header.size = sizeof(C_CHAT);
            strncpy_s(chatPkt.chat, msg, _TRUNCATE);

            send(sock, (char*)&chatPkt, sizeof(C_CHAT), 0);
        }
	}
}

BOOL WINAPI ConsoleHandler(DWORD signal)
{
	if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT)
	{
        if (g_clientSocket != INVALID_SOCKET)
        {
	        std::cout << "\n[Terminating] Sending Logout Packet..." << std::endl;

	        C_LOGOUT logoutPkt;
	        logoutPkt.header.id = PKT_C_LOGOUT;
	        logoutPkt.header.size = sizeof(C_LOGOUT);

	        send(g_clientSocket, (char*)&logoutPkt, sizeof(C_LOGOUT), 0);

            closesocket(g_clientSocket);
        }
        WSACleanup();
        exit(0);
	}
    return TRUE;
}


int main(int argc, char* argv[])
{
    // Quick Edit Mode 비활성화 — 콘솔 클릭해도 프로세스가 멈추지 않음
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(hInput, &mode);
    mode &= ~ENABLE_QUICK_EDIT_MODE;
    SetConsoleMode(hInput, mode);

    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    srand((unsigned int)time(nullptr) + GetCurrentProcessId());

    WSAData wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);

    SOCKADDR_IN serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8001);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    if (connect(clientSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cout << "Connect Error!" << std::endl;
        return 0;
    }

    g_clientSocket = clientSocket;

    std::cout << "Connected to Server!" << std::endl;

    // 로그인 정보 입력
    char inputId[32], inputPw[32];

    if (argc >= 2)
    {
        // 자동 로그인 모드: Client.exe <번호>
        // 예시: Client.exe 1 -> ID: Test_1, PW: test123
        snprintf(inputId, 32, "Test_%s", argv[1]);
        strncpy_s(inputPw, "test123", _TRUNCATE);
        std::cout << "[Auto Login] ID: " << inputId << std::endl;
    }
    else
    {
        // 수동 로그인 모드
        std::cout << "Enter User ID: ";
        std::cin >> inputId;
        std::cout << "Enter Password: ";
        std::cin >> inputPw;
    }

    C_LOGIN loginPkt;
    loginPkt.header.id = PKT_C_LOGIN;
    loginPkt.header.size = sizeof(C_LOGIN);

    memset(loginPkt.userId, 0, 32);
    memset(loginPkt.userPw, 0, 32);
    strncpy_s(loginPkt.userId, inputId, _TRUNCATE);
    strncpy_s(loginPkt.userPw, inputPw, _TRUNCATE);

    send(clientSocket, (char*)&loginPkt, sizeof(C_LOGIN), 0);
    std::cout << "Login Request Sent..." << std::endl;

    // [중요] 수신 스레드를 먼저 시작해야 로그인 응답을 받을 수 있음
    std::thread t(ReceiveThread, clientSocket);
    t.detach(); // 백그라운드에서 독립적으로 실행

    std::thread t2(InputThread, clientSocket);
    t2.detach();

    // 게임에 입장하기 전까지(myPlayerId > 0) C_MOVE 전송 대기
    while (myPlayerId == 0)
    {
        Sleep(100);
    }

    // 이후 main 스레드는 주기적으로 이동/공격 패킷을 전송
    while (true)
    {
        // 1. 이동 패킷 전송
        C_MOVE movePkt;
        memset(&movePkt, 0, sizeof(C_MOVE));

        movePkt.header.size = sizeof(C_MOVE);
        movePkt.header.id = PKT_C_MOVE;

        // 이동 좌표 랜덤 생성 (±MOVE_RANGE 범위 - 그리드 내 이동 위주)
        myX += (static_cast<float>(rand() % (MOVE_RANGE * 2) - MOVE_RANGE));
        myY += (static_cast<float>(rand() % (MOVE_RANGE * 2) - MOVE_RANGE));

        // 맵 경계 체크 (0 ~ 999)
        if (myX < 0) myX = 0;
        if (myX > 999) myX = 999;
        if (myY < 0) myY = 0;
        if (myY > 999) myY = 999;

        movePkt.x = myX;
        movePkt.y = myY;

        send(clientSocket, (char*)&movePkt, sizeof(C_MOVE), 0);

        // 2. 공격 패킷 (20% 확률)
        if (rand() % 5 == 0) {
            C_ATTACK attackPkt;
            attackPkt.header = { sizeof(C_ATTACK), PKT_C_ATTACK };

            // 확률적으로 근접(0) 또는 범위(1) 공격 선택
            attackPkt.attackType = rand() % 2;

            if (attackPkt.attackType == 0) {
                std::lock_guard<std::mutex> lock(g_listMutex);
                if (!nearbyPlayers.empty()) {
                    int randomIdx = rand() % nearbyPlayers.size();
                    attackPkt.targetId = nearbyPlayers[randomIdx];
                    send(clientSocket, (char*)&attackPkt, sizeof(C_ATTACK), 0);
                }
            }
            else {
                attackPkt.targetId = 0;
                send(clientSocket, (char*)&attackPkt, sizeof(C_ATTACK), 0);
            }
        }

        // 주기를 약간 틀어 자연스럽게 이동 (0.8초 ~ 1.2초 간격)
        Sleep(800 + rand() % 400);
    }

    closesocket(clientSocket);
    WSACleanup();
    return 0;
}
