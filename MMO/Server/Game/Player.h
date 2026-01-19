#pragma once
#include <cstdint>
#include <iostream>

#include "Session.h"


struct Pos
{
	float x;
	float y;
};

struct GridPos
{
	int x;
	int y;

	bool operator==(const GridPos& other) const
	{
		return x == other.x && y == other.y;
	}
};

class Session;
class World;

class Player
{
public:
	Player(Session* session) : m_session(session) {}

	int32_t m_Hp = 100;
	int32_t m_maxHp = 100; 

	Session* m_session = nullptr;

	void Send(char* ptr, int len);

	void Reset()
	{
		m_Hp = 100;
		m_maxHp = 100; 
	}

	void OnDamaged(int32_t damage, Player* attacker)
	{
		m_Hp -= damage;
		if (m_Hp <= 0)
		{
			m_Hp = 0;
			Die(); 
		}
	}

	void Die();

	void Respawn();

	int32_t GetPlayerId()
	{
		if (m_session == nullptr) return 0;
		return m_session->GetPlayerId();
	}
};

