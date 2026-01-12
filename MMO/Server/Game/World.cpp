#include "World.h"

#include <cppconn/driver.h>

#include "Session.h"
#include "PacketHandler.h"
#include "Protocol.h"

void World::EnterGame(Session* session)
{
	// 1. 초기 좌표 설정
	// test용으로 500, 500
	float startX = 500.0f;
	float startY = 500.0f;
	session->SetPos(startX, startY);

	GridPos pos = GetGridPos(startX, startY);

	//2. World  세션 목록에 추가
	{
		sessions[session->GetPlayerId()] = session;
		grids[pos.y][pos.x].insert(session); 
	}

	// 3. 주변 3x3에 나 알리기
	for (int dy=-1; dy<=1; dy++)
	{
		for (int dx=-1; dx <= 1; dx++)
		{
			GridPos checkPos = { pos.x + dx, pos.y + dy };
			if (IsInvalid(checkPos)) continue;

			for (Session* other : grids[checkPos.y][checkPos.x])
			{
				if (other == session) continue;

				// 양방향 스폰
				SendSpawn(other, session); // 주변 사람에게 나 알림 
				SendSpawn(session, other); // 나에게 주변 사람 알림
			}
		}
	}

	// 4. 세션 상태 변경
	// session->SetState(PlayerState::GAME);

	std::cout << "[World] Player " << session->GetPlayerId() << " -> Entered at (" << startX << ", " << startY << ")" << std::endl;
}

void World::LeaveGame(Session* session)
{
	GridPos pos = GetGridPos(session->GetX(), session->GetY());

	// 1. 주변 사람에게 나 사라졌다고 알림.
	for (int dy=-2; dy<=2; dy++)
	{
		for (int dx = -2; dx <= 2; dx++)
		{
			GridPos checkPos = { pos.x + dx, pos.y + dy };
			if (IsInvalid(checkPos)) continue;

			for (Session* other : grids[checkPos.y][checkPos.x])
			{
				if (other == session) continue;
				SendDespawn(other, session);
			}
		}
	}

	// 2. 월드 데이터에서 제거
	grids[pos.y][pos.x].erase(session);
	sessions.erase(session->GetPlayerId());

	std::cout << "[world] Player " << session->GetPlayerId() << " Left Game<<"<<std::endl;
}

void World::HandleMove(Session* session, float x, float y)
{
	GridPos oldPos = GetGridPos(session->GetX(), session->GetY());
	GridPos newPos = GetGridPos(x, y);

	session->SetPos(x, y);

	// 시야 재계산
	if (!(oldPos == newPos))
	{
		grids[oldPos.y][oldPos.x].erase(session);
		grids[newPos.y][newPos.x].insert(session);

		UpdateVision(session, oldPos, newPos); 
	}
	else
	{
		BroadcastMove(session); // 그리드 이동 없으면 주변 9칸에 MOVE 전송 
	}
}

void World::UpdateVision(Session* session, GridPos oldPos, GridPos newPos)
{
	// 1. 나를 새로 보게 된 사람들 (ENTER)
	// 현재 3x3에서 이전 3x3에 없던 사람 찾기
	for (int dy=-1; dy<=1; dy++)
	{
		for (int dx=-1; dx<=1; dx++)
		{
			GridPos cur = { newPos.x + dx, newPos.y + dy };
			if (IsInvalid(cur)) continue;

			for (Session* other : grids[cur.y][cur.x])
			{
				if (other == session) continue;

				// 새로 만난 사람
				if (abs(cur.x-oldPos.x) > 1 || abs(cur.y - oldPos.y) > 1)
				{
					SendSpawn(other, session);
					SendSpawn(session, other); 
				}
				else
				{
					SendMove(other, session); 
				}
			}
		}
	}

	// Leave 처리 - Hysterisis
	for (int dy=-1; dy <= 1; dy++)
	{
		for (int dx=-1; dx <= 1; dx++)
		{
			GridPos old = { oldPos.x + dx, oldPos.y + dy };
			if (IsInvalid(old)) continue;

			for (Session* other : grids[old.y][old.x])
			{
				if (other == session) continue;

				// 새로운 위치 기준 5x5 밖으로 멀어졌다면 LEAVE
				// Grid 특성 상, 경계선에서 플레이어가 들어왔다 나감을 반복했을 때 leave, enter가 반복 발생하는 것을 방지
				if (abs(old.x - newPos.x) > 2 || abs(old.y - newPos.y) > 2)
				{
					SendDespawn(other, session);
					SendDespawn(session, other); 
				}
			}
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

	for (int dy = -1; dy <= 1; dy++)
	{
		for (int dx=-1; dx<=1; dx++)
		{
			int nx = pos.x + dx;
			int ny = pos.y + dy;

			if (nx < 0 || nx >= GRID_COUNT || ny < 0 || ny >= GRID_COUNT) continue;

			for (Session* other : grids[ny][nx])
			{
				if (other == session) continue;
				other->Send((char*)&pkt, pkt.header.size); 
			}
		}
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


