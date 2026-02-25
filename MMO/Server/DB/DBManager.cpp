#include "DBManager.h"
#include "../Core/LogicManager.h"

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
		m_workers.emplace_back(&DBManager::DbWorkerThread, this, config);
		
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
	m_cv.notify_one();
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

		connection_properties["OPT_PLUGIN_DIR"] = "./plugin";
		connection_properties["OPT_USE_SSL"] = false;

		sql::Connection* conPtr = driver->connect(config.host, config.user, config.pw);

		if (!conPtr)
		{
			std::cout << "[DB Error] connect() returned nullptr" << std::endl;
			return;
		}

		std::unique_ptr<sql::Connection> con(conPtr);
		con->setSchema(config.db);

		std::cout << "[DB] Connection Success!" << std::endl;

		while (1)
		{
			DbTask task;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_cv.wait(lock, [this] { return m_stop || !m_tasks.empty(); });

				if (m_stop && m_tasks.empty()) return;

				task = std::move(m_tasks.front());
				m_tasks.pop(); 
			}

			if (task) {
				if (con && con->isValid()) task(con.get());
				LogicManager::Get()->GetMonitor()->AddDbCount();
			}
		}
	}
	catch (sql::SQLException& e)
	{
		std::cout << "[DB Error] Worker thread exception: " << e.what() << std::endl;
	}
}






