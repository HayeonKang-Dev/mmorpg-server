#include <WinSock2.h>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <WS2tcpip.h>

#include "Protocol.h"


#pragma comment(lib, "ws2_32.lib")

// ������ ������ ���
/*struct PacketHeader
{
	uint16_t size;	// ��Ŷ ��ü ũ�� (��� 4 + ������ 2 = 6)
	uint16_t id;	// ��Ŷ ���� (1) 
};*/

int32_t myPlayerId = 0;
std::vector<int32_t> nearbyPlayers;
std::mutex g_listMutex; // ��� ��ȣ�� �ݰ� ����

float myX = 500.0f;
float myY = 500.0f;

void ReceiveThread(SOCKET sock)
{
    std::vector<char> buffer;
    buffer.reserve(4096);
    char temp[1024];

    while (true)
    {
        // 1. �����κ��� ������ ���� (���⼭�� recv�� �մϴ�!)
        int len = recv(sock, temp, 1024, 0);
        if (len <= 0) {
            std::cout << "Disconnected from Server (ReceiveThread)." << std::endl;
            break;
        }

        buffer.insert(buffer.end(), temp, temp + len);

        // 2. ��Ŷ �ؼ� ����
        while (buffer.size() >= sizeof(PacketHeader))
        {
            PacketHeader* header = (PacketHeader*)buffer.data();
            if (buffer.size() < header->size) break;

            // --- [��Ŷ ������ ó�� �� �α� ���] ---
            if (header->id == PKT_S_LOGIN) {
                S_LOGIN* res = (S_LOGIN*)buffer.data();
                if (res->success) {
                    myPlayerId = res->playerId;
                    std::cout << "Login Success! ID: " << myPlayerId << std::endl;

                    // Send C_ENTER_GAME packet
                    PacketHeader enterGamePkt;
                    enterGamePkt.size = sizeof(PacketHeader);
                    enterGamePkt.id = PKT_C_ENTER_GAME;
                    send(sock, (char*)&enterGamePkt, sizeof(PacketHeader), 0);
                    std::cout << "Sent C_ENTER_GAME packet" << std::endl;
                }
            }
            else if (header->id == PKT_S_MOVE) {
                S_MOVE* res = (S_MOVE*)buffer.data();
                // �� �̵� �α״� ���� ������ ���� ������ �͸� ���
                if (res->playerId != myPlayerId)
                    std::cout << "[Move] Player " << res->playerId << " -> (" << res->x << ", " << res->y << ")" << std::endl;
            }
            else if (header->id == PKT_S_ATTACK) {
                S_ATTACK* res = (S_ATTACK*)buffer.data();
                if (res->targetId == myPlayerId)
                    std::cout << ">>> [ALERT] You are under attack by Player " << res->attackerId << "!" << std::endl;
                else
                    std::cout << "[Log] Player " << res->attackerId << " attacks Player " << res->targetId << std::endl;
            }
            else if (header->id == PKT_S_DIE) {
                S_DIE* res = (S_DIE*)buffer.data();
                std::cout << "!!! [DEATH] Player " << res->playerId << " has been defeated!" << std::endl;
            }
            // ReceiveThread ���� ��Ŷ ó�� �κп� �߰�
            else if (header->id == PKT_S_SPAWN) {
                S_SPAWN* pkt = (S_SPAWN*)buffer.data();
                std::lock_guard<std::mutex> lock(g_listMutex);
                // �ߺ� Ȯ�� �� �߰�
                if (std::find(nearbyPlayers.begin(), nearbyPlayers.end(), pkt->playerId) == nearbyPlayers.end()) {
                    nearbyPlayers.push_back(pkt->playerId);
                    std::cout << "[Notice] Player " << pkt->playerId << " Spawned." << std::endl;
                }
            }
            else if (header->id == PKT_S_DESPAWN || header->id == PKT_S_DIE) {
                // S_DIE�� S_DESPAWN���� �ش� ID ���� ���� (������ �����ϵ� mutex�� �߰�)
                std::lock_guard<std::mutex> lock(g_listMutex);
                // nearbyPlayers.erase(...) 
            }
            // ... SPAWN/DESPAWN �� ������ ��Ŷ�� �����ϰ� ó�� ...
            else if (header->id == PKT_S_RESPAWN)
            {
                S_RESPAWN* res = (S_RESPAWN*)buffer.data();
                std::cout << ">>> [NOTICE] Player " << res->playerId << " is Respawn!" << std::endl;
                if (res->playerId == myPlayerId)
                {
                    std::cout << ">>> [NOTICE] You have Respawned at (" << res->x << ", " << res->y << ")" << std::endl;
                    myX = res->x;
                    myY = res->y; 
                }
                else
                {
                    std::cout << ">>> [NOTICE] Player " << res->playerId << " has Respawned nearby you!" << std::endl;
                    std::lock_guard<std::mutex> lock(g_listMutex);
                    if (std::find(nearbyPlayers.begin(), nearbyPlayers.end(), res->playerId) == nearbyPlayers.end())
                    {
                        nearbyPlayers.push_back(res->playerId); 
                    }
                }
                
            }

            buffer.erase(buffer.begin(), buffer.begin() + header->size);
        }
    }
}



int main()
{
    WSAData wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);

    SOCKADDR_IN serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8001); // ���� ��Ʈ�� �´��� Ȯ���ϼ���!
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    if (connect(clientSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cout << "Connect Error!" << std::endl;
        return 0;
    }
    std::cout << "Connected to Server!" << std::endl;

    // --- [1�ܰ�: �α��� �õ�] ---
    char inputId[32], inputPw[32];
    std::cout << "Enter User ID: ";
    std::cin >> inputId;
    std::cout << "Enter Password: ";
    std::cin >> inputPw;

    C_LOGIN loginPkt;
    loginPkt.header.id = PKT_C_LOGIN;
    loginPkt.header.size = sizeof(C_LOGIN);

    memset(loginPkt.userId, 0, 32);
    memset(loginPkt.userPw, 0, 32);
    // std::string ��� ���� char �迭�� �����մϴ�.
    strncpy_s(loginPkt.userId, inputId, _TRUNCATE);
    strncpy_s(loginPkt.userPw, inputPw, _TRUNCATE);

    send(clientSocket, (char*)&loginPkt, sizeof(C_LOGIN), 0);
    std::cout << "Login Request Sent..." << std::endl;


    // --- [�߿�] �α��� ��Ŷ ���� ���� ���� ������ ���� ---
    std::thread t(ReceiveThread, clientSocket);
    t.detach(); // ��׶��忡�� ���������� ����

    

    // ���� main ������ 1�ʸ��� "������ ��"�� �մϴ�.
    while (true)
    {
        // 1. �̵� ��Ŷ �۽�
        C_MOVE movePkt;
        memset(&movePkt, 0, sizeof(C_MOVE));

        movePkt.header.size = sizeof(C_MOVE);
        movePkt.header.id = PKT_C_MOVE;

        // 1. �̵� ��ǥ ���� ��� (������ �� ����)
        myX += (static_cast<float>(rand() % 10 - 5));
        myY += (static_cast<float>(rand() % 10 - 5));

        movePkt.x = myX;
        movePkt.y = myY;

        // Y��ǥ ������ �� ����
        if (abs(movePkt.y) < 0.01f) movePkt.y = 0.0f;

        send(clientSocket, (char*)&movePkt, sizeof(C_MOVE), 0);

        // 2. ���� ���� (20% Ȯ��)
        if (rand() % 5 == 0) {
            C_ATTACK attackPkt;
            attackPkt.header = { sizeof(C_ATTACK), PKT_C_ATTACK };

            // Ȯ�������� ����(0) �Ǵ� ����(1) ���� ����
            attackPkt.attackType = rand() % 2;

            if (attackPkt.attackType == 0) { // ���� Ÿ�� ���� ����
                std::lock_guard<std::mutex> lock(g_listMutex); // ��� �����ϰ� ���
                if (!nearbyPlayers.empty()) {
                    int randomIdx = rand() % nearbyPlayers.size();
                    attackPkt.targetId = nearbyPlayers[randomIdx];
                    std::cout << ">>> [Input] Sending MELEE Attack to Player " << attackPkt.targetId << std::endl;
                    send(clientSocket, (char*)&attackPkt, sizeof(C_ATTACK), 0);
                }
            }
            else { // ���� ����
                attackPkt.targetId = 0; // Ÿ�� �ʿ� ����
                std::cout << ">>> [Input] Sending RADIAL Attack!" << std::endl;
                send(clientSocket, (char*)&attackPkt, sizeof(C_ATTACK), 0);
            }
        }

        // �ֱ⸦ �ణ Ʋ� �������� ���� (0.8�� ~ 1.2�� ����)
        Sleep(800 + rand() % 400);
    }

    closesocket(clientSocket);
    WSACleanup();
    return 0;
}