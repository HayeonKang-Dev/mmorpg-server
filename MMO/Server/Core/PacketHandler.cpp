#include "PacketHandler.h"

#include "LogicManager.h"
#include "../DB/DBManager.h"
#include "../Game/World.h"

void PacketHandler::Handle_C_LOGIN(Session* session, char* buffer)
{
    char* idPtr = buffer;
    char* pwPtr = buffer + 32;
    std::string userId(idPtr);
    std::string userPw(pwPtr); 

    if (userId.empty()) return;

    std::cout << "[Logic] Login Request - ID: " << userId << " / PW: " << userPw << std::endl;

    // DB 쿼리에 PW를 함께 넘깁니다.
    DBManager::Get()->PushQuery([session, userId, userPw](sql::Connection* con)
        {
            try
            {
                // 1. 유저 조회 (password 컬럼 포함)
                std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(
                    "SELECT user_name, level, password FROM accounts WHERE user_id = ?"));
                pstmt->setString(1, userId);
                std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

                S_LOGIN resPkt;
                resPkt.header = { sizeof(S_LOGIN), PKT_S_LOGIN };
                bool isExist = res->next();

                if (isExist)
                {
                    // --- 기존 유저 존재: 비번 체크 ---
                    std::string dbPw = res->getString("password");

                    if (dbPw == userPw) // [핵심] 비번 일치 확인
                    {
                        std::string name = res->getString("user_name");
                        int level = res->getInt("level");
                        std::cout << "[DB] Password Match! User: " << name << std::endl;

                        // 로그인 시각 업데이트
                        std::unique_ptr<sql::PreparedStatement> updatePstmt(con->prepareStatement(
                            "UPDATE accounts SET last_login = NOW() WHERE user_id = ?"));
                        updatePstmt->setString(1, userId);
                        updatePstmt->executeUpdate();

                        resPkt.success = true;
                        resPkt.level = level;
                        strncpy_s(resPkt.name, name.c_str(), _TRUNCATE);
                    }
                    else
                    {
                        // [실패] 비번 틀림
                        std::cout << "[DB] Password Mismatch for ID: " << userId << std::endl;
                        resPkt.success = false;
                    }
                }
                else
                {
                    // --- 신규 유저: 입력한 비번으로 가입 ---
                    std::unique_ptr<sql::PreparedStatement> insertPstmt(con->prepareStatement(
                        "INSERT INTO accounts(user_id, password, user_name, level) VALUES (?, ?, ?, ?)"));
                    insertPstmt->setString(1, userId);
                    insertPstmt->setString(2, userPw); // 입력받은 비번 저장
                    insertPstmt->setString(3, userId);
                    insertPstmt->setInt(4, 1);
                    insertPstmt->executeUpdate();
                    con->commit();

                    std::cout << "[DB] New User Registered: " << userId << " with PW: " << userPw << std::endl;

                    resPkt.success = true;
                    resPkt.level = 1;
                    strncpy_s(resPkt.name, userId.c_str(), _TRUNCATE);
                }

                // 결과 배달...
                Job dbResultJob;
                dbResultJob.session = session;
                dbResultJob.header = resPkt.header;
                char* ptr = reinterpret_cast<char*>(&resPkt);
                dbResultJob.data.assign(ptr, ptr + sizeof(S_LOGIN));
                LogicManager::Get()->PushJob(std::move(dbResultJob));
            }
            catch (sql::SQLException& e) { std::cerr << "[DB Error] " << e.what() << std::endl; }
        });
}

void PacketHandler::Handle_C_MOVE(Session* session, char* buffer)
{
	// 1. 상태 체크 (GAME) 상태에만 이동 가능
	// 2. 이동 로직 수행 (좌표 갱신)
	// 3. AOI(시야) 체크 후 주변 유저에게만 S_MOVE 브로드캐스트

	C_MOVE* pkt = reinterpret_cast<C_MOVE*>(buffer);

	if (session->GetState() != PlayerState::GAME) return;

	float* posX = reinterpret_cast<float*>(buffer);
	float* posY = reinterpret_cast<float*>(buffer + 4);
	std::cout << "[Logic] C_MOVE: Player " << session->GetPlayerId() << " -> Pos(" << *posX << ", " << *posY << ")" << std::endl;

	World::Get()->HandleMove(session, pkt->x, pkt->y);



}

void PacketHandler::Handle_C_ENTER_GAME(Session* session, char* buffer)
{
	// 1. 상태 체크
	if (session->GetState() != PlayerState::LOBBY) return;

	// 2. 월드 입장
	World::Get()->EnterGame(session); 
}

void PacketHandler::Handle_S_LOGIN(Session* session, char* buffer)
{
	if (session == nullptr || buffer == nullptr) return;

	// [Logic Threa] DB에서 인증 마치고 돌아온 시점
	std::cout << "[Logic] DB Search Success! Setting PlayerState" << std::endl;

	// 1. 전달받은 버퍼(Job.data)를 S_LOGIN 구조체로 해석
	// buffer에는 헤더가 포함된 전체 S_LOGIN 데이터 들어있음..
	S_LOGIN* pkt = reinterpret_cast<S_LOGIN*>(buffer);

	// 2. 클라이언트에 실제 응답 패킷 전송
	session->Send((char*)pkt, sizeof(S_LOGIN));

	// 3. 인게임 입장
	session->SetState(PlayerState::GAME);
	World::Get()->EnterGame(session);

	std::cout << "[Logic] Login Success & World Entered. " << std::endl;
}
