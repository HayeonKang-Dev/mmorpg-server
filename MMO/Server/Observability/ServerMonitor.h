#pragma once
#include <fstream>
#include <iostream>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <atomic>
#include <vector>
#include <windows.h>
#include <psapi.h>

static const int MAX_IOCP_WORKERS = 4;

class ServerMonitor {
private:
    std::ofstream _file;
    std::string _filename;
    uint64_t _lastTick = 0;
    std::atomic<uint32_t> _recvCount{ 0 };
    std::atomic<uint32_t> _sendCount{ 0 };

    // CPU 측정용 (인스턴스 멤버로 변경)
    ULARGE_INTEGER _lastKernel{};
    ULARGE_INTEGER _lastUser{};
    uint64_t _lastCpuTime = 0;

    // 스레드별 작업량 카운터
    std::atomic<uint32_t> _iocpCount[MAX_IOCP_WORKERS]{};
    std::atomic<uint32_t> _logicCount{ 0 };
    std::atomic<uint32_t> _dbCount{ 0 };

    // 사용 가능한 파일명 찾기 (중복 시 번호 붙이기)
    std::string FindAvailableFilename(const std::string& baseName) {
        std::string name = baseName + ".csv";
        if (!std::ifstream(name).good()) {
            return name;  // 파일이 없으면 그대로 사용
        }

        // 파일이 있으면 번호 붙이기
        for (int i = 1; i <= 9999; i++) {
            name = baseName + "_" + std::to_string(i) + ".csv";
            if (!std::ifstream(name).good()) {
                return name;
            }
        }
        return baseName + "_overflow.csv";  // fallback
    }

public:
    ServerMonitor() {
        // 중복되지 않는 파일명 찾기
        _filename = FindAvailableFilename("server_performance_log");

        _file.open(_filename);
        _file << std::unitbuf; // 자동 flush

        // 새 파일이므로 헤더 작성
        _file << "Timestamp,CCU,Recv_PPS,Send_PPS,Memory_MB,CPU_Percent,IOCP_0,IOCP_1,IOCP_2,IOCP_3,Logic_Jobs,DB_Queries\n";

        std::cout << "[Monitor] Log file: " << _filename << std::endl;

        _lastTick = GetTickCount64();
    }

    ~ServerMonitor() {
        if (_file.is_open()) _file.close();
    }

    void AddRecvCount() { _recvCount.fetch_add(1, std::memory_order_relaxed); }
    void AddSendCount() { _sendCount.fetch_add(1, std::memory_order_relaxed); }
    void AddWorkerCount(int idx) { if (idx >= 0 && idx < MAX_IOCP_WORKERS) _iocpCount[idx].fetch_add(1, std::memory_order_relaxed); }
    void AddLogicCount() { _logicCount.fetch_add(1, std::memory_order_relaxed); }
    void AddDbCount() { _dbCount.fetch_add(1, std::memory_order_relaxed); }

    void Update(int32_t currentCCU) {
        uint64_t now = GetTickCount64();
        if (now - _lastTick >= 1000) {
            // atomic swap으로 카운터 리셋하면서 값 가져오기
            uint32_t recv = _recvCount.exchange(0, std::memory_order_relaxed);
            uint32_t send = _sendCount.exchange(0, std::memory_order_relaxed);

            uint32_t iocp[MAX_IOCP_WORKERS];
            for (int i = 0; i < MAX_IOCP_WORKERS; i++)
                iocp[i] = _iocpCount[i].exchange(0, std::memory_order_relaxed);
            uint32_t logic = _logicCount.exchange(0, std::memory_order_relaxed);
            uint32_t db = _dbCount.exchange(0, std::memory_order_relaxed);

            _file << GetTimestamp() << ","
                << currentCCU << ","
                << recv << ","
                << send << ","
                << std::fixed << std::setprecision(2) << GetMemoryUsageMB() << ","
                << std::fixed << std::setprecision(1) << GetProcessCPU() << ","
                << iocp[0] << "," << iocp[1] << "," << iocp[2] << "," << iocp[3] << ","
                << logic << "," << db << "\n";

            _lastTick = now;
        }
    }

private:
    std::string GetTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        struct tm timeInfo;
        localtime_s(&timeInfo, &time); // 안전한 대안 사용
        std::stringstream ss;
        ss << std::put_time(&timeInfo, "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    float GetMemoryUsageMB() {
        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(),
            (PROCESS_MEMORY_COUNTERS*)&pmc,
            sizeof(pmc))) {
            return pmc.PrivateUsage / (1024.0f * 1024.0f);
        }
        return 0.0f;
    }

    // 프로세스 전용 CPU 사용률 (시스템 전체가 아닌 이 프로세스만)
    float GetProcessCPU() {
        FILETIME creation, exit, kernel, user;
        if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
            return 0.0f;
        }

        ULARGE_INTEGER k, u;
        k.LowPart = kernel.dwLowDateTime;
        k.HighPart = kernel.dwHighDateTime;
        u.LowPart = user.dwLowDateTime;
        u.HighPart = user.dwHighDateTime;

        uint64_t now = GetTickCount64();

        // 첫 호출 시 기준값 저장
        if (_lastCpuTime == 0) {
            _lastKernel = k;
            _lastUser = u;
            _lastCpuTime = now;
            return 0.0f;
        }

        uint64_t cpuDiff = (k.QuadPart - _lastKernel.QuadPart) + (u.QuadPart - _lastUser.QuadPart);
        uint64_t timeDiff = (now - _lastCpuTime) * 10000; // ms -> 100ns 단위

        float usage = 0.0f;
        if (timeDiff > 0) {
            // CPU 코어 수로 나눠서 전체 대비 비율로 표시
            SYSTEM_INFO sysInfo;
            GetSystemInfo(&sysInfo);
            int numCores = sysInfo.dwNumberOfProcessors;

            usage = ((cpuDiff / (float)timeDiff) * 100.0f) / numCores;
        }

        _lastKernel = k;
        _lastUser = u;
        _lastCpuTime = now;

        return min(usage, 100.0f);
    }
};
