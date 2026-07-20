#pragma once
#include "DatabaseConnector.hpp"
#include <string>
#include <vector>
#include <map>

namespace SincroPro {

class ODBCConnector : public DatabaseConnector {
public:
    ODBCConnector();
    ~ODBCConnector() override;

    bool connect(const std::string& connectionStringOrUrl) override;
    void disconnect() override;
    bool isConnected() const override;

    std::vector<std::string> getTables() override;
    std::vector<std::pair<std::string, std::string>> getColumns(const std::string& tableName) override;
    
    std::vector<std::map<std::string, std::string>> executeQuery(const std::string& queryOrEndpoint) override;
    std::string executeInsert(const std::string& tableNameOrEndpoint, const std::map<std::string, std::string>& data) override;
    bool executeUpdate(const std::string& tableNameOrEndpoint, const std::map<std::string, std::string>& data, const std::string& keyColumn, const std::string& keyValue) override;

private:
    void* m_env = nullptr;  // SQLHENV
    void* m_dbc = nullptr;  // SQLHDBC
    bool m_connected = false;

    std::string getLastError(void* handle, int handleType);
};

} // namespace SincroPro
