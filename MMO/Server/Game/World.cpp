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
	GridPos pos = GetGridPos(session->GetX(), session->GetY());

	// 1. �ֺ� ������� �� ������ٰ� �˸�.
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

	// 2. ���� �����Ϳ��� ����
	grids[pos.y][pos.x].erase(session);
	sessions.erase(session->GetPlayerId());

	std::cout << "[world] Player " << session->GetPlayerId() << " Left Game<<"<<std::endl;
}

void World::HandleMove(Session* session, float x, float y)
{
	GridPos oldPos = GetGridPos(session->GetX(), session->GetY());
	GridPos newPos = GetGridPos(x, y);

	session->SetPos(x, y);

	// �þ� ����
	if (!(oldPos == newPos))
	{
		grids[oldPos.y][oldPos.x].erase(session);
		grids[newPos.y][newPos.x].insert(session);

		UpdateVision(session, oldPos, newPos); 
	}
	else
	{
		BroadcastMove(session); // �׸��� �̵� ������ �ֺ� 9ĭ�� MOVE ���� 
	}
}

void World::UpdateVision(Session* session, GridPos oldPos, GridPos newPos)
{
	// 1. ���� ���� ���� �� ����� (ENTER)
	// ���� 3x3���� ���� 3x3�� ���� ��� ã��
	for (int dy=-1; dy<=1; dy++)
	{
		for (int dx=-1; dx<=1; dx++)
		{
			GridPos cur = { newPos.x + dx, newPos.y + dy };
			if (IsInvalid(cur)) continue;

			for (Session* other : grids[cur.y][cur.x])
			{
				if (other == session) continue;

				// ���� ���� ���
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

	// Leave ó�� - Hysterisis
	for (int dy=-1; dy <= 1; dy++)
	{
		for (int dx=-1; dx <= 1; dx++)
		{
			GridPos old = { oldPos.x + dx, oldPos.y + dy };
			if (IsInvalid(old)) continue;

			for (Session* other : grids[old.y][old.x])
			{
				if (other == session) continue;

				// ���ο� ��ġ ���� 5x5 ������ �־����ٸ� LEAVE
				// Grid Ư�� ��, ��輱���� �÷��̾ ���Դ� ������ �ݺ����� �� leave, enter�� �ݺ� �߻��ϴ� ���� ����
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


