#include "World.h"

#include <cppconn/driver.h>

#include "Session.h"
#include "PacketHandler.h"
#include "Protocol.h"

// Helper: 시야 범위 내 모든 그리드에 세션 등록
void World::InsertToVisionGrids(Session* session, GridPos centerPos, int visionRange)
{
	for (int dy = -visionRange; dy <= visionRange; dy++)
	{
		for (int dx = -visionRange; dx <= visionRange; dx++)
		{
			GridPos pos = { centerPos.x + dx, centerPos.y + dy };
			if (IsInvalid(pos)) continue;
			grids[pos.y][pos.x].insert(session);
		}
	}
}

// Helper: 시야 범위 내 모든 그리드에서 세션 제거
void World::RemoveFromVisionGrids(Session* session, GridPos centerPos, int visionRange)
{
	for (int dy = -visionRange; dy <= visionRange; dy++)
	{
		for (int dx = -visionRange; dx <= visionRange; dx++)
		{
			GridPos pos = { centerPos.x + dx, centerPos.y + dy };
			if (IsInvalid(pos)) continue;
			grids[pos.y][pos.x].erase(session);
		}
	}
}

// Helper: 시야 이동 시 겹치지 않는 부분만 제거/추가 (최적화!)
void World::UpdateVisionGrids(Session* session, GridPos oldPos, GridPos newPos, int visionRange)
{
	// 1. 새 시야에 없는 이전 그리드에서 제거
	for (int dy = -visionRange; dy <= visionRange; dy++)
	{
		for (int dx = -visionRange; dx <= visionRange; dx++)
		{
			GridPos oldGrid = { oldPos.x + dx, oldPos.y + dy };
			if (IsInvalid(oldGrid)) continue;

			// 새 시야 범위 밖이면 제거
			if (abs(oldGrid.x - newPos.x) > visionRange || abs(oldGrid.y - newPos.y) > visionRange)
			{
				grids[oldGrid.y][oldGrid.x].erase(session);
			}
		}
	}

	// 2. 이전 시야에 없던 새 그리드에 추가
	for (int dy = -visionRange; dy <= visionRange; dy++)
	{
		for (int dx = -visionRange; dx <= visionRange; dx++)
		{
			GridPos newGrid = { newPos.x + dx, newPos.y + dy };
			if (IsInvalid(newGrid)) continue;

			// 이전 시야 범위 밖이면 추가
			if (abs(newGrid.x - oldPos.x) > visionRange || abs(newGrid.y - oldPos.y) > visionRange)
			{
				grids[newGrid.y][newGrid.x].insert(session);
			}
		}
	}
}

void World::EnterGame(Session* session)
{
	// 0. Player 객체 생성 (아직 없다면)
	if (session->GetPlayer() == nullptr)
	{
		Player* newPlayer = new Player(session);
		session->SetPlayer(newPlayer);
		std::cout << "[World] Player object created for ID " << session->GetPlayerId() << std::endl;
	}

	float startX = 500.0f;
	float startY = 500.0f;
	session->SetPos(startX, startY);

	GridPos centerPos = GetGridPos(startX, startY);
	int visionRange = 1;

	// 1. 내 시야 9칸 모두에 나를 등록
	InsertToVisionGrids(session, centerPos, visionRange);

	// 2. 내 실제 위치 그리드의 플레이어들과 상호 SPAWN
	for (Session* other : grids[centerPos.y][centerPos.x])
	{
		if (other == session) continue;
		SendSpawn(other, session);
		SendSpawn(session, other);
	}

	sessions[session->GetPlayerId()] = session;

	std::cout << "[World] Player " << session->GetPlayerId() << " -> Entered at (" << startX << ", " << startY << ")" << std::endl;
}

void World::LeaveGame(Session* session)
{
	if (session == nullptr || session->GetPlayer() == nullptr) return;

	int32_t leavePlayerId = session->GetPlayerId();
	GridPos pos = GetGridPos(session->GetX(), session->GetY());

	{
		std::lock_guard<std::mutex> lock(_gridMutex[pos.y][pos.x]);
		for (Session* observer : grids[pos.y][pos.x])
		{
			if (observer == session) continue;
			SendDespawn(observer, session); 
		}
	}

	RemoveFromVisionGrids(session, pos, VISION_RANGE);
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		sessions.erase(leavePlayerId); 
	}
	std::cout << "[World] Player " << leavePlayerId << " Leave Success." << std::endl;
}

void World::HandleMove(Session* session, float x, float y)
{
	GridPos oldPos = GetGridPos(session->GetX(), session->GetY());
	GridPos newPos = GetGridPos(x, y);

	session->SetPos(x, y);

	int visionRange = 1;

	if (!(oldPos == newPos))
	{
		// 겹치지 않는 부분만 제거/추가 (최적화!)
		UpdateVisionGrids(session, oldPos, newPos, visionRange);
		UpdateVision(session, oldPos, newPos);
	}
	else
	{
		// 같은 그리드 내 이동 → 내 실제 위치 그리드만 Broadcast
		BroadcastMove(session);
	}
}

void World::UpdateVision(Session* session, GridPos oldPos, GridPos newPos)
{
	// 1. 새로 보이게 된 플레이어 처리 (SPAWN/MOVE)
	for (Session* other : grids[newPos.y][newPos.x])
	{
		if (other == session) continue;

		// 이전 위치에서 1칸 이상 벗어났다면 → 새로 보임
		if (abs(newPos.x - oldPos.x) > 1 || abs(newPos.y - oldPos.y) > 1)
		{
			SendSpawn(other, session);
			SendSpawn(session, other);
		}
		else
		{
			// 계속 보이던 애는 MOVE만
			SendMove(other, session);
		}
	}

	// 2. 시야에서 벗어난 플레이어 처리 (DESPAWN) 
	for (Session* other : grids[oldPos.y][oldPos.x])
	{
		if (other == session) continue;

		// 새 위치에서 2칸 이상 멀어졌다면 → DESPAWN
		if (abs(oldPos.x - newPos.x) > 2 || abs(oldPos.y - newPos.y) > 2)
		{
			SendDespawn(other, session);
			SendDespawn(session, other);
		}
	}
}

void World::BroadcastMove(Session* session)
{
	GridPos pos = GetGridPos(session->GetX(), session->GetY());

	S_MOVE pkt;
	pkt.header.id = PKT_S_MOVE;
	pkt.header.size = sizeof(S_MOVE);
	pkt.playerId = session->GetPlayerId();
	pkt.x = session->GetX();
	pkt.y = session->GetY();

	BroadcastPacketToObservers(session, (char*)&pkt, pkt.header.size); 
}

void World::BroadcastDie(Session* session)
{
	GridPos pos = GetGridPos(session->GetX(), session->GetY());

	S_DIE pkt;
	pkt.header = { sizeof(S_DIE), PKT_S_DIE };
	pkt.playerId = session->GetPlayerId();

	BroadcastPacketToObservers(session, (char*)&pkt, pkt.header.size);

	std::cout << "[World] Player " << session->GetPlayerId() << " Died. Broadcast sent to observers in grid [" << pos.x << ", " << pos.y << "]" << std::endl;

}

void World::BroadcastPacketToObservers(Session* session, char* ptr, int len)
{
	GridPos pos = GetGridPos(session->GetX(), session->GetY());

	for (Session* observer : grids[pos.y][pos.x])
	{
		if (observer == nullptr) continue;
		observer->Send(ptr, len); 
	}
}

void World::SendSpawn(Session* target, Session* obj)
{
	if (target == obj) return;

	S_SPAWN pkt;
	pkt.header = { sizeof(S_SPAWN), PKT_S_SPAWN };
	pkt.playerId = obj->GetPlayerId();
	pkt.x = obj->GetX();
	pkt.y = obj->GetY();
	target->Send((char*)&pkt, pkt.header.size);
}

void World::SendDespawn(Session* target, Session* obj)
{
	if (target == obj) return;

	S_DESPAWN pkt;
	pkt.header = { sizeof(S_DESPAWN), PKT_S_DESPAWN };
	pkt.playerId = obj->GetPlayerId();
	target->Send((char*)&pkt, pkt.header.size);
}

float World::CalculateDistance(Session* s1, Session* s2)
{
	if (s1 == nullptr || s2 == nullptr) return 99999.0f;
	float dx = s1->GetX() - s2->GetX();
	float dy = s1->GetY() - s2->GetY();
	return std::sqrt(dx * dx + dy * dy);
}

Session* World::FindSession(int32_t playerId)
{
	auto it = sessions.find(playerId);
	if (it == sessions.end()) return nullptr;

	return it->second;
}

