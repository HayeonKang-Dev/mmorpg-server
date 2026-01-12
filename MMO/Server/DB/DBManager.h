#pragma once
#include <mysql_driver.h>
#include <mysql_connection.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <thread>
#include <queue>
#include <functional>
#include <mutex>

using DbTask = std::function<void(sql::Connection*)>;
struct DBConfig
{
	char host[256];
	char user[64];
	char pw[64];
	char db[64];
};
class DBManager
{
public:
	static DBManager* Get()
	{
		static DBManager instance;
		return &instance; 
	}

	// DB 스레드 개수와 접속 정보 초기화
	bool Init(int workerCount, const std::string& host, const std::string& user, const std::string& pw, const std::string& db);

	// 외부(로직 스레드)에서 비동기로 쿼리 던질 때 호출
	void PushQuery(DbTask task);

	void Shutdown(); 

private:
	// 각 DB 워커 스레드가 실제로 실행하는 루프 함수
	void DbWorkerThread(DBConfig config);

	std::vector<std::thread> m_workers;
	std::queue<DbTask> m_tasks;
	std::mutex m_mutex;
	std::condition_variable m_cv;
	bool m_stop = false; 
};

