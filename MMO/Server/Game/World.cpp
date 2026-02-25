#include "World.h"

#include <cppconn/driver.h>
#include <cstdlib>

#include "Session.h"
#include "PacketHandler.h"
#include "Protocol.h"

// Worker 스레드(LeaveGame)와 Logic 스레드(HandleMove 등)의 동시 grid/sessions 접근 방지
std::recursive_mutex g_worldMutex;

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

			if (abs(newGrid.x - oldPos.x) > visionRange || abs(newGrid.y - oldPos.y) > visionRange)
			{
				grids[newGrid.y][newGrid.x].insert(session);
			}
		}
	}
}

void World::EnterGame(Session* session)
{
	float startX, startY;
	GridPos centerPos;
	std::vector<Session*> spawnTargets; // 상호 SPAWN 대상

	{
		std::lock_guard<std::recursive_mutex> lock(g_worldMutex);

		if (session->GetPlayer() == nullptr)
		{
			Player* newPlayer = new Player(session);
			session->SetPlayer(newPlayer);
		}

		startX = (float)(rand() % MAP_SIZE);
		startY = (float)(rand() % MAP_SIZE);
		session->SetPos(startX, startY);

		centerPos = GetGridPos(startX, startY);

		// 내 시야 9칸 모두에 나를 등록
		InsertToVisionGrids(session, centerPos, VISION_RANGE);

		// 상호 SPAWN 대상 수집 (내 실제 위치 그리드)
		for (Session* other : grids[centerPos.y][centerPos.x])
		{
			if (other != session)
				spawnTargets.push_back(other);
		}

		sessions[session->GetPlayerId()] = session;
	}

	// mutex 바깥에서 전송
	// 본인에게 스폰 위치 전송
	{
		S_SPAWN selfSpawn;
		selfSpawn.header = { sizeof(S_SPAWN), PKT_S_SPAWN };
		selfSpawn.playerId = session->GetPlayerId();
		selfSpawn.x = startX;
		selfSpawn.y = startY;
		session->Send((char*)&selfSpawn, sizeof(selfSpawn));
	}

	// 상호 SPAWN 전송
	S_SPAWN spawnPkt;
	spawnPkt.header = { sizeof(S_SPAWN), PKT_S_SPAWN };
	for (Session* other : spawnTargets)
	{
		// other에게 나를 알림
		spawnPkt.playerId = session->GetPlayerId();
		spawnPkt.x = startX;
		spawnPkt.y = startY;
		other->Send((char*)&spawnPkt, sizeof(spawnPkt));

		// 나에게 other를 알림
		spawnPkt.playerId = other->GetPlayerId();
		spawnPkt.x = other->GetX();
		spawnPkt.y = other->GetY();
		session->Send((char*)&spawnPkt, sizeof(spawnPkt));
	}
}

void World::LeaveGame(Session* session)
{
	std::vector<Session*> despawnTargets;
	int32_t leavePlayerId;

	{
		std::lock_guard<std::recursive_mutex> lock(g_worldMutex);
		if (session == nullptr || session->GetPlayer() == nullptr) return;

		leavePlayerId = session->GetPlayerId();
		GridPos pos = GetGridPos(session->GetX(), session->GetY());

		// DESPAWN 대상 수집
		for (Session* observer : grids[pos.y][pos.x])
		{
			if (observer != session)
				despawnTargets.push_back(observer);
		}

		RemoveFromVisionGrids(session, pos, VISION_RANGE);
		sessions.erase(leavePlayerId);
	}

	// mutex 바깥에서 DESPAWN 전송
	S_DESPAWN pkt;
	pkt.header = { sizeof(S_DESPAWN), PKT_S_DESPAWN };
	pkt.playerId = leavePlayerId;
	for (Session* observer : despawnTargets)
		observer->Send((char*)&pkt, sizeof(pkt));
}

void World::HandleMove(Session* session, float x, float y)
{
	// mutex 보유 구간: 좌표/그리드 업데이트 + 전송 대상 수집만
	bool gridChanged = false;
	GridPos oldPos, newPos;

	// 전송 태스크
	std::vector<Session*> moveTargets;
	std::vector<std::pair<Session*, Session*>> despawnPairs; // (target, obj)
	std::vector<std::pair<Session*, Session*>> spawnPairs;

	{
		std::lock_guard<std::recursive_mutex> lock(g_worldMutex);

		// 좌표 클램핑
		if (x < 0.0f) x = 0.0f;
		if (x >= (float)MAP_SIZE) x = (float)MAP_SIZE - 1.0f;
		if (y < 0.0f) y = 0.0f;
		if (y >= (float)MAP_SIZE) y = (float)MAP_SIZE - 1.0f;

		oldPos = GetGridPos(session->GetX(), session->GetY());
		newPos = GetGridPos(x, y);

		session->SetPos(x, y);

		if (!(oldPos == newPos))
		{
			gridChanged = true;
			UpdateVisionGrids(session, oldPos, newPos, VISION_RANGE);

			// DESPAWN 대상 수집: 새 시야 밖으로 나간 셀
			for (int dy = -VISION_RANGE; dy <= VISION_RANGE; dy++)
			{
				for (int dx = -VISION_RANGE; dx <= VISION_RANGE; dx++)
				{
					GridPos oldGrid = { oldPos.x + dx, oldPos.y + dy };
					if (IsInvalid(oldGrid)) continue;
					if (abs(oldGrid.x - newPos.x) > VISION_RANGE || abs(oldGrid.y - newPos.y) > VISION_RANGE)
					{
						for (Session* other : grids[oldGrid.y][oldGrid.x])
						{
							if (other != session)
							{
								despawnPairs.push_back({ other, session });
								despawnPairs.push_back({ session, other });
							}
						}
					}
				}
			}

			// SPAWN 대상 수집: 새로 시야에 들어온 셀
			for (int dy = -VISION_RANGE; dy <= VISION_RANGE; dy++)
			{
				for (int dx = -VISION_RANGE; dx <= VISION_RANGE; dx++)
				{
					GridPos newGrid = { newPos.x + dx, newPos.y + dy };
					if (IsInvalid(newGrid)) continue;
					if (abs(newGrid.x - oldPos.x) > VISION_RANGE || abs(newGrid.y - oldPos.y) > VISION_RANGE)
					{
						for (Session* other : grids[newGrid.y][newGrid.x])
						{
							if (other != session)
							{
								spawnPairs.push_back({ other, session });
								spawnPairs.push_back({ session, other });
							}
						}
					}
				}
			}
		}

		// MOVE 브로드캐스트 대상 수집 (현재 위치 그리드)
		GridPos curPos = GetGridPos(session->GetX(), session->GetY());
		for (Session* obs : grids[curPos.y][curPos.x])
		{
			if (obs != nullptr)
				moveTargets.push_back(obs);
		}
	}

	// mutex 바깥에서 전송

	// DESPAWN
	{
		S_DESPAWN pkt;
		pkt.header = { sizeof(S_DESPAWN), PKT_S_DESPAWN };
		for (auto& [target, obj] : despawnPairs)
		{
			pkt.playerId = obj->GetPlayerId();
			target->Send((char*)&pkt, sizeof(pkt));
		}
	}

	// SPAWN
	{
		S_SPAWN pkt;
		pkt.header = { sizeof(S_SPAWN), PKT_S_SPAWN };
		for (auto& [target, obj] : spawnPairs)
		{
			pkt.playerId = obj->GetPlayerId();
			pkt.x = obj->GetX();
			pkt.y = obj->GetY();
			target->Send((char*)&pkt, sizeof(pkt));
		}
	}

	// MOVE
	{
		S_MOVE pkt;
		pkt.header = { sizeof(S_MOVE), PKT_S_MOVE };
		pkt.playerId = session->GetPlayerId();
		pkt.x = x;
		pkt.y = y;
		for (Session* obs : moveTargets)
			obs->Send((char*)&pkt, sizeof(pkt));
	}
}

// UpdateVision은 HandleMove 내부로 통합됐으므로 빈 구현 유지 (헤더 호환)
void World::UpdateVision(Session* session, GridPos oldPos, GridPos newPos)
{
	// 현재 HandleMove에서 인라인 처리됨
}

void World::BroadcastMove(Session* session)
{
	// 현재 HandleMove에서 인라인 처리됨 (호환성 유지)
	std::vector<Session*> targets;
	{
		std::lock_guard<std::recursive_mutex> lock(g_worldMutex);
		GridPos pos = GetGridPos(session->GetX(), session->GetY());
		for (Session* obs : grids[pos.y][pos.x])
			if (obs) targets.push_back(obs);
	}
	S_MOVE pkt;
	pkt.header = { sizeof(S_MOVE), PKT_S_MOVE };
	pkt.playerId = session->GetPlayerId();
	pkt.x = session->GetX();
	pkt.y = session->GetY();
	for (Session* obs : targets)
		obs->Send((char*)&pkt, sizeof(pkt));
}

void World::BroadcastDie(Session* session)
{
	std::vector<Session*> targets;
	{
		std::lock_guard<std::recursive_mutex> lock(g_worldMutex);
		GridPos pos = GetGridPos(session->GetX(), session->GetY());
		for (Session* obs : grids[pos.y][pos.x])
			if (obs) targets.push_back(obs);
	}
	S_DIE pkt;
	pkt.header = { sizeof(S_DIE), PKT_S_DIE };
	pkt.playerId = session->GetPlayerId();
	for (Session* obs : targets)
		obs->Send((char*)&pkt, sizeof(pkt));
}

void World::BroadcastPacketToObservers(Session* session, char* ptr, int len)
{
	// mutex 보유 시간 최소화: 타겟 목록만 snapshot, Send는 mutex 바깥에서
	std::vector<Session*> targets;
	{
		std::lock_guard<std::recursive_mutex> lock(g_worldMutex);
		GridPos pos = GetGridPos(session->GetX(), session->GetY());
		targets.reserve(grids[pos.y][pos.x].size());
		for (Session* observer : grids[pos.y][pos.x])
		{
			if (observer != nullptr) targets.push_back(observer);
		}
	}
	for (Session* observer : targets)
		observer->Send(ptr, len);
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

void World::BroadcastToAll(char* ptr, int len)
{
	std::vector<Session*> targets;
	{
		std::lock_guard<std::recursive_mutex> lock(g_worldMutex);
		targets.reserve(sessions.size());
		for (auto& [id, session] : sessions)
			if (session != nullptr) targets.push_back(session);
	}
	for (Session* s : targets)
		s->Send(ptr, len);
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
	std::lock_guard<std::recursive_mutex> lock(g_worldMutex);
	auto it = sessions.find(playerId);
	if (it == sessions.end()) return nullptr;

	return it->second;
}

void World::LogAllPlayersAOI()
{
	std::lock_guard<std::recursive_mutex> lock(g_worldMutex);
	for (auto& [id, session] : sessions)
	{
		GridPos pos = GetGridPos(session->GetX(), session->GetY());

		std::vector<int32_t> nearbyIds;
		for (Session* other : grids[pos.y][pos.x])
		{
			if (other != session)
				nearbyIds.push_back(other->GetPlayerId());
		}

		_aoiLogger.LogPlayerAOI(id, session->GetX(), session->GetY(), nearbyIds);
	}
}
