#include "SyncEngine.hpp"
#include "ODBCConnector.hpp"
#include "APIConnector.hpp"
#include <fstream>
#include <iostream>
#include <chrono>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;

namespace SincroPro {

static std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

SyncEngine::SyncEngine(const std::string& configPath)
    : m_configPath(configPath), m_idTracker("id_mappings.db") {
    m_idTracker.initialize();
}

SyncEngine::~SyncEngine() {
    stopAll();
}

bool SyncEngine::loadConfig() {
    std::ifstream file(m_configPath);
    if (!file.is_open()) {
        addLog("SyncEngine: No config file found. Starting fresh.");
        return false;
    }

    try {
        json j;
        file >> j;
        
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        m_sessions.clear();

        if (j.contains("sessions") && j["sessions"].is_array()) {
            for (const auto& sObj : j["sessions"]) {
                SyncSessionConfig s;
                s.name = sObj.value("name", "Unnamed Session");
                s.dbA_connStr = sObj.value("dbA_connStr", "");
                s.dbB_url = sObj.value("dbB_url", "");
                s.intervalSeconds = sObj.value("intervalSeconds", 60);
                s.active = sObj.value("active", false);

                if (sObj.contains("mappings") && sObj["mappings"].is_array()) {
                    for (const auto& mObj : sObj["mappings"]) {
                        TableMapping tm;
                        tm.tableA = mObj.value("tableA", "");
                        tm.tableB = mObj.value("tableB", "");
                        tm.direction = mObj.value("direction", "A_TO_B");
                        tm.idGenerator = mObj.value("idGenerator", false);

                        if (mObj.contains("columns") && mObj["columns"].is_array()) {
                            for (const auto& cObj : mObj["columns"]) {
                                ColumnMapping cm;
                                cm.colA = cObj.value("colA", "");
                                cm.colB = cObj.value("colB", "");
                                cm.isKey = cObj.value("isKey", false);
                                cm.translateViaTable = cObj.value("translateViaTable", "");
                                tm.columns.push_back(cm);
                            }
                        }
                        s.mappings.push_back(tm);
                    }
                }
                m_sessions.push_back(s);
            }
        }
        addLog("SyncEngine: Configuration loaded successfully.");
        return true;
    } catch (const std::exception& e) {
        addLog("SyncEngine: Error parsing config file: " + std::string(e.what()));
        return false;
    }
}

bool SyncEngine::saveConfig() {
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    return saveConfigInternal();
}

bool SyncEngine::saveConfigInternal() {
    json j = json::object();
    j["sessions"] = json::array();

    for (const auto& s : m_sessions) {
        json sObj = json::object();
        sObj["name"] = s.name;
        sObj["dbA_connStr"] = s.dbA_connStr;
        sObj["dbB_url"] = s.dbB_url;
        sObj["intervalSeconds"] = s.intervalSeconds;
        sObj["active"] = s.active;

        sObj["mappings"] = json::array();
        for (const auto& tm : s.mappings) {
            json mObj = json::object();
            mObj["tableA"] = tm.tableA;
            mObj["tableB"] = tm.tableB;
            mObj["direction"] = tm.direction;
            mObj["idGenerator"] = tm.idGenerator;

            mObj["columns"] = json::array();
            for (const auto& cm : tm.columns) {
                json cObj = json::object();
                cObj["colA"] = cm.colA;
                cObj["colB"] = cm.colB;
                cObj["isKey"] = cm.isKey;
                cObj["translateViaTable"] = cm.translateViaTable;
                mObj["columns"].push_back(cObj);
            }
            sObj["mappings"].push_back(mObj);
        }
        j["sessions"].push_back(sObj);
    }

    std::ofstream file(m_configPath);
    if (!file.is_open()) {
        addLog("SyncEngine: Failed to open config file for saving.");
        return false;
    }

    file << j.dump(4);
    return true;
}

void SyncEngine::startSession(const std::string& sessionName) {
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    
    // Check if session is already running
    if (m_sessionThreads.find(sessionName) != m_sessionThreads.end()) {
        return;
    }

    // Find session in configs
    for (auto& s : m_sessions) {
        if (s.name == sessionName) {
            s.active = true;
            auto stopFlag = std::make_shared<std::atomic<bool>>(false);
            m_sessionStopFlags[sessionName] = stopFlag;
            m_sessionThreads[sessionName] = std::thread(&SyncEngine::sessionThreadFunc, this, sessionName, stopFlag);
            addLog("SyncEngine: Started session thread [" + sessionName + "]");
            break;
        }
    }
    
    // Save state update
    std::thread([this]() { saveConfig(); }).detach();
}

void SyncEngine::stopSession(const std::string& sessionName) {
    std::shared_ptr<std::atomic<bool>> stopFlag = nullptr;
    std::thread sessionThread;

    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        auto itFlag = m_sessionStopFlags.find(sessionName);
        if (itFlag != m_sessionStopFlags.end()) {
            stopFlag = itFlag->second;
            stopFlag->store(true);
        }

        auto itThread = m_sessionThreads.find(sessionName);
        if (itThread != m_sessionThreads.end()) {
            sessionThread = std::move(itThread->second);
            m_sessionThreads.erase(itThread);
        }
        m_sessionStopFlags.erase(sessionName);

        for (auto& s : m_sessions) {
            if (s.name == sessionName) {
                s.active = false;
                break;
            }
        }
    }

    if (sessionThread.joinable()) {
        sessionThread.join();
        addLog("SyncEngine: Stopped session thread [" + sessionName + "]");
    }

    saveConfig();
}

void SyncEngine::runSyncOnce(const std::string& sessionName) {
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    for (auto& s : m_sessions) {
        if (s.name == sessionName) {
            std::thread([this, s]() mutable {
                addLog("SyncEngine: Executing single sync run for [" + s.name + "]...");
                executeSyncProcess(s);
                addLog("SyncEngine: Finished single sync run for [" + s.name + "].");
            }).detach();
            break;
        }
    }
}

void SyncEngine::startAll() {
    std::vector<std::string> sessionsToStart;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        for (const auto& s : m_sessions) {
            if (s.active) {
                sessionsToStart.push_back(s.name);
            }
        }
    }

    for (const auto& name : sessionsToStart) {
        // Start using public function to avoid lock issues
        startSession(name);
    }
}

void SyncEngine::stopAll() {
    std::vector<std::string> sessionsToStop;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        for (const auto& pair : m_sessionThreads) {
            sessionsToStop.push_back(pair.first);
        }
    }

    for (const auto& name : sessionsToStop) {
        stopSession(name);
    }
}

bool SyncEngine::isSessionRunning(const std::string& sessionName) {
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    return m_sessionThreads.find(sessionName) != m_sessionThreads.end();
}

void SyncEngine::addSession(const SyncSessionConfig& session) {
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    m_sessions.push_back(session);
    saveConfigInternal();
}

void SyncEngine::updateSession(const std::string& oldName, const SyncSessionConfig& session) {
    bool wasRunning = isSessionRunning(oldName);
    if (wasRunning) {
        stopSession(oldName);
    }

    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        for (auto& s : m_sessions) {
            if (s.name == oldName) {
                s = session;
                break;
            }
        }
    }

    saveConfig();

    if (wasRunning || session.active) {
        startSession(session.name);
    }
}

void SyncEngine::deleteSession(const std::string& sessionName) {
    stopSession(sessionName);

    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
            if (it->name == sessionName) {
                m_sessions.erase(it);
                break;
            }
        }
    }
    m_idTracker.clearSessionMappings(sessionName);
    saveConfig();
}

std::vector<std::string> SyncEngine::getLogs() {
    std::lock_guard<std::mutex> lock(m_logsMutex);
    return m_logs;
}

void SyncEngine::addLog(const std::string& msg) {
    std::lock_guard<std::mutex> lock(m_logsMutex);
    std::string timestamped = "[" + getCurrentTimestamp() + "] " + msg;
    m_logs.push_back(timestamped);
    std::cout << timestamped << std::endl;
    // Limit to 200 messages in memory console
    if (m_logs.size() > 200) {
        m_logs.erase(m_logs.begin());
    }
}

void SyncEngine::sessionThreadFunc(const std::string& sessionName, std::shared_ptr<std::atomic<bool>> stopFlag) {
    SyncSessionConfig session;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        for (const auto& s : m_sessions) {
            if (s.name == sessionName) {
                session = s;
                break;
            }
        }
    }

    addLog("Session [" + sessionName + "] sync thread is running.");

    while (!stopFlag->load()) {
        executeSyncProcess(session);

        // Sleep in small steps to react to stopFlag quickly
        for (int i = 0; i < session.intervalSeconds * 10; ++i) {
            if (stopFlag->load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    addLog("Session [" + sessionName + "] sync thread has exited.");
}

void SyncEngine::executeSyncProcess(SyncSessionConfig& session) {
    addLog("Session [" + session.name + "]: Starting synchronization pass.");

    // Connect databases dynamically based on protocol
    std::unique_ptr<DatabaseConnector> dbA;
    if (session.dbA_connStr.rfind("http://", 0) == 0 || session.dbA_connStr.rfind("https://", 0) == 0) {
        dbA = std::make_unique<APIConnector>();
    } else {
        dbA = std::make_unique<ODBCConnector>();
    }

    std::unique_ptr<DatabaseConnector> dbB;
    if (session.dbB_url.rfind("http://", 0) == 0 || session.dbB_url.rfind("https://", 0) == 0) {
        dbB = std::make_unique<APIConnector>();
    } else {
        dbB = std::make_unique<ODBCConnector>();
    }

    if (!dbA->connect(session.dbA_connStr)) {
        addLog("Session [" + session.name + "]: Error: Failed to connect to BD A: " + session.dbA_connStr);
        return;
    }
    if (!dbB->connect(session.dbB_url)) {
        addLog("Session [" + session.name + "]: Error: Failed to connect to BD B: " + session.dbB_url);
        dbA->disconnect();
        return;
    }

    // Run table mappings in order (crucial for dependencies)
    for (const auto& tm : session.mappings) {
        addLog("Session [" + session.name + "]: Syncing table " + tm.tableA + " -> " + tm.tableB);

        // Find primary key column of Table A
        std::string keyColA = "";
        std::string keyColB = "";
        for (const auto& col : tm.columns) {
            if (col.isKey) {
                keyColA = col.colA;
                keyColB = col.colB;
                break;
            }
        }

        if (keyColA.empty() || keyColB.empty()) {
            addLog("Session [" + session.name + "]: Error: Mapeo de " + tm.tableA + " a " + tm.tableB + " no tiene llave principal definida.");
            continue;
        }

        // Fetch data from BD A
        std::stringstream query;
        query << "SELECT * FROM " << tm.tableA;
        std::vector<std::map<std::string, std::string>> rows = dbA->executeQuery(query.str());
        
        int insertCount = 0;
        int updateCount = 0;

        for (const auto& row : rows) {
            std::string sourceIdVal = "";
            auto itKey = row.find(keyColA);
            if (itKey != row.end()) {
                sourceIdVal = itKey->second;
            }

            if (sourceIdVal.empty()) {
                continue; // Skip rows without key values
            }

            // Build target columns insert/update payload
            std::map<std::string, std::string> targetData;
            for (const auto& col : tm.columns) {
                // If it's the primary key of B and it's an auto-generated identity mapping, skip sending it to insert payload 
                // if it hasn't been mapped yet, let the API generate it.
                if (col.isKey && tm.idGenerator) {
                    continue;
                }

                auto itVal = row.find(col.colA);
                if (itVal != row.end()) {
                    std::string valueToSync = itVal->second;
                    
                    // Apply ID translator mapping rule
                    if (!col.translateViaTable.empty()) {
                        std::string translatedId = m_idTracker.getTargetId(session.name, col.translateViaTable, valueToSync);
                        if (!translatedId.empty()) {
                            valueToSync = translatedId;
                        } else {
                            // If the relation ID mapping is missing (e.g. referenced user wasn't synced yet),
                            // we log a warning and continue, but we can also sync it with NULL or let it fail or log.
                            addLog("Session [" + session.name + "]: Warning: Mapeo de ID perdido para columna " + col.colB + " con valor original " + itVal->second);
                        }
                    }
                    targetData[col.colB] = valueToSync;
                }
            }

            // Check if this row is already translated (i.e. has a synced counterpart in target)
            std::string targetIdVal = m_idTracker.getTargetId(session.name, tm.tableA, sourceIdVal);

            if (!targetIdVal.empty()) {
                // UPDATE target
                bool ok = dbB->executeUpdate(tm.tableB, targetData, keyColB, targetIdVal);
                if (ok) {
                    updateCount++;
                } else {
                    addLog("Session [" + session.name + "]: Error updating target row " + targetIdVal + " in " + tm.tableB);
                }
            } else {
                // INSERT target
                std::string generatedId = dbB->executeInsert(tm.tableB, targetData);
                if (!generatedId.empty()) {
                    insertCount++;
                    // Register translation mapping (even if tm.idGenerator is false, we can save it to track synced rows)
                    m_idTracker.registerMapping(session.name, tm.tableA, sourceIdVal, generatedId);
                } else {
                    addLog("Session [" + session.name + "]: Error inserting row into " + tm.tableB);
                }
            }
        }
        addLog("Session [" + session.name + "]: Sync stats for " + tm.tableA + ": " + std::to_string(insertCount) + " insertados, " + std::to_string(updateCount) + " actualizados.");
    }

    dbA->disconnect();
    dbB->disconnect();
    addLog("Session [" + session.name + "]: Synchronization pass completed.");
}

} // namespace SincroPro
