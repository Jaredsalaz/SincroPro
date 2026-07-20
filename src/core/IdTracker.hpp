#pragma once
#include <string>
#include <vector>

// Forward declaration of sqlite3 struct in the global namespace
struct sqlite3;

namespace SincroPro {

class IdTracker {
public:
    IdTracker(const std::string& dbPath = "id_mappings.db");
    ~IdTracker();

    bool initialize();

    // Register a translation mapping between Source ID (BD A) and Target ID (BD B)
    bool registerMapping(const std::string& sessionName, 
                         const std::string& tableName, 
                         const std::string& sourceId, 
                         const std::string& targetId);

    // Retrieve the target ID mapped to the source ID
    std::string getTargetId(const std::string& sessionName, 
                            const std::string& tableName, 
                            const std::string& sourceId);

    // Retrieve the source ID mapped to the target ID
    std::string getSourceId(const std::string& sessionName, 
                            const std::string& tableName, 
                            const std::string& targetId);

    // Delete all mappings for a specific session
    bool clearSessionMappings(const std::string& sessionName);

private:
    std::string m_dbPath;
    struct sqlite3* m_db;
};

} // namespace SincroPro
