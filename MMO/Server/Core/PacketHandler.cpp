#include "PacketHandler.h"

#include "LogicManager.h"
#include "../DB/DBManager.h"
#include "../Game/World.h"

static int32_t g_IdCounter = 1; 

void PacketHandler::Handle_C_LOGIN(Session* session, char* buffer)
{
    char* idPtr = buffer;
    char* pwPtr = buffer + 32;
    std::string userId(idPtr);
    std::string userPw(pwPtr); 

    if (userId.empty()) return;

    std::cout << "[Logic] Login Request - ID: " << userId << " / PW: " << userPw << std::endl;

    // DB ������ PW�� �Բ� �ѱ�ϴ�.
    DBManager::Get()->PushQuery([session, userId, userPw](sql::Connection* con)
        {
            try
            {
                // 1. ���� ��ȸ (password �÷� ����)
                std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(
                    "SELECT user_name, level, password FROM accounts WHERE user_id = ?"));
                pstmt->setString(1, userId);
                std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

                S_LOGIN resPkt;
                resPkt.header = { sizeof(S_LOGIN), PKT_S_LOGIN };
                bool isExist = res->next();

                if (isExist)
                {
                    // --- ���� ���� ����: ��� üũ ---
                    std::string dbPw = res->getString("password");

                    if (dbPw == userPw) // [�ٽ�] ��� ��ġ Ȯ��
                    {
                        // ���� ID �Ҵ�
                        int32_t assignedId = g_IdCounter++;
                        session->SetPlayerId(assignedId); 

                        std::string name = res->getString("user_name");
                        int level = res->getInt("level");
                        std::cout << "[DB] Password Match! User: " << name << std::endl;

                        // �α��� �ð� ������Ʈ
                        std::unique_ptr<sql::PreparedStatement> updatePstmt(con->prepareStatement(
                            "UPDATE accounts SET last_login = NOW() WHERE user_id = ?"));
                        updatePstmt->setString(1, userId);
                        updatePstmt->executeUpdate();

                        resPkt.success = true;
                        resPkt.level = level;
                        resPkt.playerId = assignedId;
                        strncpy_s(resPkt.name, name.c_str(), _TRUNCATE);
                    }
                    else
                    {
                        // [����] ��� Ʋ��
                        std::cout << "[DB] Password Mismatch for ID: " << userId << std::endl;
                        resPkt.success = false;
                    }
                }
                else
                {
                    // --- �ű� ����: �Է��� ������� ���� ---
                    std::unique_ptr<sql::PreparedStatement> insertPstmt(con->prepareStatement(
                        "INSERT INTO accounts(user_id, password, user_name, level) VALUES (?, ?, ?, ?)"));
                    insertPstmt->setString(1, userId);
                    insertPstmt->setString(2, userPw); // �Է¹��� ��� ����
                    insertPstmt->setString(3, userId);
                    insertPstmt->setInt(4, 1);
                    insertPstmt->executeUpdate();
                    con->commit();

                    // ���� ID �Ҵ� 
                    int32_t assignedId = g_IdCounter++;
                    session->SetPlayerId(assignedId);


                    std::cout << "[DB] New User Registered: " << userId << " with PW: " << userPw << std::endl;

                    resPkt.success = true;
                    resPkt.level = 1;
                    strncpy_s(resPkt.name, userId.c_str(), _TRUNCATE);
                }

                // DB 쿼리 성공 및 패킷 준비 완료 후
                Job* dbResultJob = JobPool::Pop();
                dbResultJob->session = session;
                dbResultJob->header = resPkt.header;

                char* ptr = reinterpret_cast<char*>(&resPkt);
                dbResultJob->data.assign(ptr, ptr + sizeof(S_LOGIN));

                LogicManager::Get()->PushJob(dbResultJob);
            }
            catch (sql::SQLException& e) { std::cerr << "[DB Error] " << e.what() << std::endl; }
        });
}

void PacketHandler::Handle_C_MOVE(Session* session, char* buffer)
{
    C_MOVE* pkt = reinterpret_cast<C_MOVE*>(buffer);
    Player* player = session->GetPlayer();
    

    if (player == nullptr)
    {
    	
        std::cout << "[Error] C_MOVE: Player is nullptr for session " << session->GetPlayerId() << std::endl;
        return;
    }
	if (player->m_Hp <= 0) return;

    std::cout << "[Logic] C_MOVE: Player " << player->GetPlayerId() << " -> Pos: (" << pkt->x << ", " << pkt->y << ")" << std::endl;

    // World::HandleMove를 호출해야 그리드 업데이트 및 브로드캐스트가 처리됨!
    World::Get()->HandleMove(session, pkt->x, pkt->y);
}

void PacketHandler::Handle_C_ENTER_GAME(Session* session, char* buffer)
{
	// 1. State 체크
	if (session->GetState() != PlayerState::LOBBY)
	{
		std::cout << "[Warning] C_ENTER_GAME: Player " << session->GetPlayerId() << " is not in LOBBY state" << std::endl;
		return;
	}

	std::cout << "[Logic] C_ENTER_GAME: Player " << session->GetPlayerId() << " entering game..." << std::endl;

	// 2. 게임 입장
	session->SetState(PlayerState::GAME);
	World::Get()->EnterGame(session);
}

void PacketHandler::Handle_S_LOGIN(Session* session, char* buffer)
{
	if (session == nullptr || buffer == nullptr) return;

	std::cout << "[Logic] DB Search Success! Setting PlayerState" << std::endl;

	S_LOGIN* pkt = reinterpret_cast<S_LOGIN*>(buffer);

	// Send login response to client
	session->Send((char*)pkt, sizeof(S_LOGIN));

	// Set state to LOBBY (NOT GAME yet - wait for C_ENTER_GAME packet)
	session->SetState(PlayerState::LOBBY);

	std::cout << "[Logic] Login Success. Waiting for C_ENTER_GAME..." << std::endl;
}

void PacketHandler::Handle_C_ATTACK(Session* session, char* buffer)
{
    C_ATTACK* pkt = reinterpret_cast<C_ATTACK*>(buffer);
    Player* attacker = session->GetPlayer();
    if (attacker == nullptr || attacker->m_Hp <= 0) return;

    if (pkt->attackType == 0)
    {
        Session* targetSession = World::Get()->FindSession(pkt->targetId);
        if (targetSession)
        {
            float dist = World::CalculateDistance(session, targetSession);
            if (dist <= ATTACK_RANGE_MEL)
            {
	            Player* target = targetSession->GetPlayer();
				target->OnDamaged(10, attacker);

                S_ATTACK res;
                res.header = { sizeof(S_ATTACK), PKT_S_ATTACK };
                res.attackerId = attacker->GetPlayerId();
                res.targetId = target->GetPlayerId();

                World::Get()->BroadcastPacketToObservers(session, (char*)&res, sizeof(res));
                std::cout << "[Combat] Player " << res.attackerId << " attacks Player " << res.targetId << std::endl;
                
            }
            
        }
    }
    else if (pkt->attackType == 1)
    {
        std::cout << "[Combat] Player " << attacker->GetPlayerId() << " uses RAIDAL Attack!" << std::endl;
        GridPos pos = World::Get()->GetGridPos(session->GetX(), session->GetY());
        
		for (Session* other : World::Get()->GetGrids()[pos.y][pos.x])
		{
            if (other == session) continue;
            float dist = World::CalculateDistance(session, other);
            if (dist <= ATTACK_RANGE_RADIAL)
            {
                if (other->GetPlayer() != nullptr)
                {
	                other->GetPlayer()->OnDamaged(20, attacker);

	                S_ATTACK res;
	                res.header = { sizeof(S_ATTACK), PKT_S_ATTACK };
	                res.attackerId = attacker->GetPlayerId();
	                res.targetId = other->GetPlayerId();

	                World::Get()->BroadcastPacketToObservers(session, (char*)&res, sizeof(res)); 
                }
                
            }
		}
    }
}

void PacketHandler::Handle_C_CHAT(Session* session, char* buffer)
{
    Player* player = session->GetPlayer();
    if (player == nullptr) return;

    std::cout << "[Chat] Player " << player->GetPlayerId() << ": " << buffer << std::endl;

    S_CHAT res;
    memset(&res, 0, sizeof(S_CHAT));
    res.header.id = PKT_S_CHAT;
    res.header.size = sizeof(S_CHAT);
    res.playerId = player->GetPlayerId();

    memcpy(res.chat, buffer, 128);

    World::Get()->BroadcastPacketToObservers(session, (char*)&res, sizeof(S_CHAT)); 
}

void PacketHandler::Handle_C_LOGOUT(Session* session, char* buffer)
{
	Player* player = session->GetPlayer();
	if (player == nullptr) return;

	std::cout << "[Logout] Player : " << player->GetPlayerId() << " requested logout" << std::endl;
	World::Get()->LeaveGame(session);
	SessionManager::Get()->Release(session); 
}

