#include "Player.h"
#include "World.h"

void Player::Die()
{


	m_Hp = 0;

	World::Get()->BroadcastDie(m_session);

	Session* session = m_session;
	int32_t expectedId = m_session->GetPlayerId();

	World::Get()->GetTimeWheel()->AddTimer(5000, [session, expectedId]()
	{
		Player* player = session->GetPlayer();
		if (player == nullptr) return;
		if (session->GetPlayerId() != expectedId) return;


		player->Respawn();
	});

}

void Player::Respawn()
{
	m_Hp = m_maxHp;

	float rx = m_session->GetX();
	float ry = m_session->GetY();

	S_RESPAWN pkt;
	pkt.header = { sizeof(S_RESPAWN), PKT_S_RESPAWN };
	pkt.playerId = m_session->GetPlayerId();
	pkt.x = rx;
	pkt.y = ry;
	m_session->Send((char*)&pkt, sizeof(pkt)); 
	World::Get()->BroadcastPacketToObservers(m_session, (char*)&pkt, sizeof(pkt)); 
}
