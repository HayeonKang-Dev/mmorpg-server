#include "Player.h"
#include "World.h"

void Player::Die()
{
	std::cout << "Player " << m_session->GetPlayerId() << " is Dead!" << std::endl;

	m_Hp = 0;

	World::Get()->BroadcastDie(m_session);

	// ReSpawn
	World::Get()->GetTimeWheel()->AddTimer(5000, [this]()
		{
			std::cout << "Player " << m_session->GetPlayerId() << " is Respawning...." << std::endl;
			this->Respawn();
		});

}

void Player::Respawn()
{
	m_Hp = m_maxHp;

	float rx = 500.0f;
	float ry = 500.0f;

	World::Get()->HandleMove(m_session, rx, ry);

	S_RESPAWN pkt;
	pkt.header = { sizeof(S_RESPAWN), PKT_S_RESPAWN };
	pkt.playerId = m_session->GetPlayerId();
	pkt.x = rx;
	pkt.y = ry;
	m_session->Send((char*)&pkt, sizeof(pkt)); // 나에게도 전송 
	World::Get()->BroadcastPacketToObservers(m_session, (char*)&pkt, sizeof(pkt)); 
}
