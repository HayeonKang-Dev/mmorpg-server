#pragma once
#include <chrono>
#include <fstream>
#include <vector>
#include <iomanip>
#include <string>

class AOILogger
{
private:
	std::ofstream _file;

	std::string FindAvailableFilename(const std::string& baseName) {
		std::string name = baseName + ".csv";
		if (!std::ifstream(name).good()) return name;

		for (int i = 1; i <= 9999; i++) {
			name = baseName + "_" + std::to_string(i) + ".csv";
			if (!std::ifstream(name).good()) return name;
		}
		return baseName + "_overflow.csv";
	}

public:
	AOILogger()
	{
		std::string filename = FindAvailableFilename("player_aoi_list");
		_file.open(filename);
		_file << "Timestamp,MyID,Pos_X,Pos_Y,Nearby_Count,Nearby_IDs\n";
	}

	void LogPlayerAOI(int32_t myId, float x, float y, const std::vector<int32_t>& nearbyIds)
	{
		if (!_file.is_open()) return;

		auto now = std::chrono::system_clock::now();
		auto time = std::chrono::system_clock::to_time_t(now);
		struct tm t;

		localtime_s(&t, &time);

		_file << std::put_time(&t, "%H:%M:%S") << ","
			<< myId << "," << std::fixed << std::setprecision(1) << x << "," << y << "," << nearbyIds.size() << ",";

		_file << "\"";
		for (size_t i = 0; i<nearbyIds.size(); ++i)
		{
			_file << nearbyIds[i] << (i == nearbyIds.size() - 1 ? "" : " "); 
		}
		_file << "\"\n";
	}
};