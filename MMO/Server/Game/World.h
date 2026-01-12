#pragma once
#include <map>
#include <mutex>
#include <set>

#include "Player.h"
#include "PacketHandler.h"

class Session;

const int MAP_SIZE = 1000;
const int GRID_SIZE = 100;
const int GRID_COUNT = MAP_SIZE / GRID_SIZE; // 10칸

class World
{
public:
	static World* Get() { static World instance; return &instance; }

	// 게임에 진입
	void EnterGame(Session* session);

	// 나갈 때
	void LeaveGame(Session* session);

	// 이동할 때
	void HandleMove(Session* session, float x, float y);
	void UpdateVision(Session* session, GridPos oldPos, GridPos newPos); 

	// 주변 칸에 패킷 전송
	void BroadcastMove(Session* session); 

	void SendSpawn(Session* target, Session* obj);
	void SendDespawn(Session* target, Session* obj); 

private:
	GridPos GetGridPos(float x, float y)
	{
		int gx = (int)x / GRID_SIZE;
		int gy = (int)y / GRID_SIZE;
		if (gx < 0) gx = 0;
		if (gx >= GRID_COUNT) gx = GRID_COUNT - 1;
		if (gy < 0) gy = 0;
		if (gy >= GRID_COUNT) gy = GRID_COUNT - 1;
		return { gx, gy };
	}
	bool IsInvalid(GridPos pos) { return (pos.x < 0 || pos.y < 0 || pos.x >= GRID_COUNT || pos.y >= GRID_COUNT); }

	void SendMove(Session* target, Session* obj)
	{
		if (target == obj) return;
		S_MOVE pkt;
		pkt.header = { sizeof(S_MOVE), PKT_S_MOVE };
		pkt.playerId = obj->GetPlayerId();
		pkt.x = obj->GetX();
		pkt.y = obj->GetY();
		target->Send((char*)&pkt, pkt.header.size); 
	}
	std::set<Session*> grids[GRID_COUNT][GRID_COUNT];
	std::map<int32_t, Session*> sessions;
	//std::mutex m_mutex; 

};

