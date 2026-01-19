#pragma once
#include <mutex>
#include <vector>

#include "Player.h"

class PlayerManager
{
public:
	static PlayerManager* Get() { static PlayerManager instance; return &instance; }

	Player* Acquire(Session* session)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_PlayerPool.empty())
		{
			for (int i = 0; i < 10; i++) m_PlayerPool.push_back(new Player(nullptr)); 
		}
		Player* player = m_PlayerPool.back();
		m_PlayerPool.pop_back();
		player->m_session = session;
		player->Reset(); // HP 등 데이터 초기화
		return player;
	}

	void Release(Player* player)
	{
		if (player == nullptr) return;
		std::lock_guard<std::mutex> lock(m_mutex);
		player->m_session = nullptr;
		m_PlayerPool.push_back(player); 
	}


private:
	std::vector<Player*> m_PlayerPool;
	std::mutex m_mutex; 
};

