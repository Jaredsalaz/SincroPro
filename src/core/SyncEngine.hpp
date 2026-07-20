#pragma once
#include "DatabaseConnector.hpp"
#include "IdTracker.hpp"
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <nlohmann/json.hpp>

namespace SincroPro {

struct ColumnMapping {
    std::string colA;
    std::string colB;
    bool isKey = false;
    std::string translateViaTable = ""; // Generic translation mapping reference table
};

struct TableMapping {
    std::string tableA;
    std::string tableB;
    std::string direction; // "A_TO_B", "B_TO_TO", "BIDIRECTIONAL"
    bool idGenerator = false;
    std::vector<ColumnMapping> columns;
};

struct SyncSessionConfig {
    std::string name;
    std::string dbA_connStr;
    std::string dbB_url;
    int intervalSeconds = 60; // Scheduler interval
    std::vector<TableMapping> mappings;
    bool active = false;
};

class SyncEngine {
public:
    SyncEngine(const std::string& configPath = "config.json");
    ~SyncEngine();

    bool loadConfig();
    bool saveConfig();

    void startSession(const std::string& sessionName);
    void stopSession(const std::string& sessionName);
    void runSyncOnce(const std::string& sessionName);

    void startAll();
    void stopAll();

    bool isSessionRunning(const std::string& sessionName);
    
    // Config management API for GUI
    std::vector<SyncSessionConfig>& getSessions() { return m_sessions; }
    void addSession(const SyncSessionConfig& session);
    void updateSession(const std::string& oldName, const SyncSessionConfig& session);
    void deleteSession(const std::string& sessionName);

    // Dynamic Logging Console
    std::vector<std::string> getLogs();
    void addLog(const std::string& msg);

private:
    std::string m_configPath;
    std::vector<SyncSessionConfig> m_sessions;
    IdTracker m_idTracker;
    
    std::mutex m_sessionsMutex;
    std::map<std::string, std::thread> m_sessionThreads;
    std::map<std::string, std::shared_ptr<std::atomic<bool>>> m_sessionStopFlags;

    std::mutex m_logsMutex;
    std::vector<std::string> m_logs;

    bool saveConfigInternal();
    void sessionThreadFunc(const std::string& sessionName, std::shared_ptr<std::atomic<bool>> stopFlag);
    void executeSyncProcess(SyncSessionConfig& session);
};

} // namespace SincroPro
