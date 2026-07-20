#include "IdTracker.hpp"
#include <sqlite3.h>
#include <iostream>

namespace SincroPro {

IdTracker::IdTracker(const std::string& dbPath) 
    : m_dbPath(dbPath), m_db(nullptr) {
}

IdTracker::~IdTracker() {
    if (m_db) {
        sqlite3_close(m_db);
    }
}

bool IdTracker::initialize() {
    int rc = sqlite3_open(m_dbPath.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }

    const char* sql = "CREATE TABLE IF NOT EXISTS id_mappings ("
                      "session_name TEXT, "
                      "table_name TEXT, "
                      "source_id TEXT, "
                      "target_id TEXT, "
                      "PRIMARY KEY (session_name, table_name, source_id)"
                      ");"
                      "CREATE INDEX IF NOT EXISTS idx_target_id ON id_mappings (session_name, table_name, target_id);";

    char* errMsg = nullptr;
    rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }

    return true;
}

bool IdTracker::registerMapping(const std::string& sessionName, 
                               const std::string& tableName, 
                               const std::string& sourceId, 
                               const std::string& targetId) {
    if (!m_db) return false;

    const char* sql = "INSERT OR REPLACE INTO id_mappings (session_name, table_name, source_id, target_id) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Prepare error: " << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }

    sqlite3_bind_text(stmt, 1, sessionName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, tableName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, sourceId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, targetId.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

std::string IdTracker::getTargetId(const std::string& sessionName, 
                                  const std::string& tableName, 
                                  const std::string& sourceId) {
    if (!m_db) return "";

    const char* sql = "SELECT target_id FROM id_mappings WHERE session_name = ? AND table_name = ? AND source_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return "";

    sqlite3_bind_text(stmt, 1, sessionName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, tableName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, sourceId.c_str(), -1, SQLITE_TRANSIENT);

    std::string targetId;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* val = sqlite3_column_text(stmt, 0);
        if (val) {
            targetId = reinterpret_cast<const char*>(val);
        }
    }

    sqlite3_finalize(stmt);
    return targetId;
}

std::string IdTracker::getSourceId(const std::string& sessionName, 
                                  const std::string& tableName, 
                                  const std::string& targetId) {
    if (!m_db) return "";

    const char* sql = "SELECT source_id FROM id_mappings WHERE session_name = ? AND table_name = ? AND target_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return "";

    sqlite3_bind_text(stmt, 1, sessionName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, tableName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, targetId.c_str(), -1, SQLITE_TRANSIENT);

    std::string sourceId;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* val = sqlite3_column_text(stmt, 0);
        if (val) {
            sourceId = reinterpret_cast<const char*>(val);
        }
    }

    sqlite3_finalize(stmt);
    return sourceId;
}

bool IdTracker::clearSessionMappings(const std::string& sessionName) {
    if (!m_db) return false;

    const char* sql = "DELETE FROM id_mappings WHERE session_name = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, sessionName.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

} // namespace SincroPro
