#include "DBManager.h"

bool DBManager::Init(int workerCount, const std::string& host, const std::string& user, const std::string& pw,
	const std::string& db)
{
	DBConfig config = {};
	strncpy_s(config.host, host.c_str(), _TRUNCATE);
	strncpy_s(config.user, user.c_str(), _TRUNCATE);
	strncpy_s(config.pw, pw.c_str(), _TRUNCATE);
	strncpy_s(config.db, db.c_str(), _TRUNCATE);

	for (int i=0; i<workerCount; i++)
	{
		// workerCount만큼 워커 스레드 생성
		m_workers.emplace_back(&DBManager::DbWorkerThread, this, config);
		// [추가] 드라이버 초기화 경합 방지 위해 간격 
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	return true; 
}

void DBManager::PushQuery(DbTask task)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_tasks.push(task); 
	}
	m_cv.notify_one(); // 기다리고 있는 스레드 한개 깨우기
}

void DBManager::Shutdown()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_stop = true; 
	}
	m_cv.notify_all();

	for (auto& t : m_workers)
	{
		if (t.joinable()) t.join(); 
	}
}

void DBManager::DbWorkerThread(DBConfig config)
{
	try
	{
		sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();
		sql::ConnectOptionsMap connection_properties;
		connection_properties["hostName"] = std::string(config.host);
		connection_properties["userName"] = std::string(config.user);
		connection_properties["password"] = std::string(config.pw);
		connection_properties["SCHEMA"] = std::string(config.db);

		// [중요] 플러그인 폴더 위치를 알려줍니다. 
		// 현재 실행파일 경로 기준의 plugin 폴더를 바라보게 합니다.
		connection_properties["OPT_PLUGIN_DIR"] = "./plugin";
		connection_properties["OPT_USE_SSL"] = false; // 일단 안전하게 SSL은 끕니다.

		// sql::Connection* conPtr = driver->connect(connection_properties);
		sql::Connection* conPtr = driver->connect(config.host, config.user, config.pw);

		if (!conPtr) return;

		std::unique_ptr<sql::Connection> con(conPtr);
		con->setSchema(config.db);

		std::cout << "[DB] Connection Success!" << std::endl;

		while (1)
		{
			DbTask task;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				// 작업 있거나 종료 신호 올 때 까지 대기
				m_cv.wait(lock, [this] { return m_stop || !m_tasks.empty(); });

				if (m_stop && m_tasks.empty()) return;

				task = std::move(m_tasks.front());
				m_tasks.pop(); 
			}

			// 쿼리 실행 (전달받은 람다 함수 실행)
			if (task) {
				if (con && con->isValid()) task(con.get());
			}
		}
	}
	catch (sql::SQLException& e)
	{
		std::cerr << "DB Worker Thread Error: " << e.what() << std::endl;
	}
}






