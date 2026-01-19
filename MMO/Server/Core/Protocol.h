// Protocol.h
#pragma once
#include <cstdint>

// 1. 패킷 ID 정의 (이름 충돌 방지를 위해 PKT_ 추가)
enum PacketID : uint16_t
{
    PKT_C_LOGIN = 1,
    PKT_S_LOGIN = 2,
    PKT_C_MOVE = 3,
    PKT_S_MOVE = 4,
    PKT_C_ENTER_GAME = 5,
    PKT_S_SPAWN = 6,    // 나중에 쓸 것들 미리 추가
    PKT_S_DESPAWN = 7,
    PKT_S_DIE = 8,
    PKT_S_RESPAWN = 9,
    PKT_C_ATTACK = 10,
    PKT_S_ATTACK = 11,
};

#pragma pack(push, 1)
// 2. 패킷 헤더 (이미 다른 곳에 있다면 제외, 보통 여기서 같이 관리)
struct PacketHeader
{
    uint16_t size;
    uint16_t id;
};

// 3. 실제 패킷 구조체
struct C_LOGIN
{
    PacketHeader header;
    char userId[32];
    char userPw[32]; 
};

struct S_LOGIN
{
    PacketHeader header;
    bool success;
    int level;
    char name[32];
    int32_t playerId;
};

struct C_MOVE
{
    PacketHeader header;
    float x;
    float y;
};

struct S_MOVE
{
    PacketHeader header;
    int32_t playerId;
    float x;
    float y;
};

struct S_SPAWN
{
    PacketHeader header;
    int32_t playerId;
    float x;
    float y;
};

struct S_DESPAWN
{
    PacketHeader header;
    int32_t playerId;
};

struct C_ATTACK
{
    PacketHeader header;
    int32_t attackType;
    int32_t targetId; 
};

struct S_ATTACK
{
    PacketHeader header;
    int32_t attackerId;
    int32_t targetId;
};

struct S_DIE
{
    PacketHeader header;
    int32_t playerId; 
};

struct S_RESPAWN
{
    PacketHeader header;
    int32_t playerId;
    float x;
    float y;
};


#pragma pack(pop)