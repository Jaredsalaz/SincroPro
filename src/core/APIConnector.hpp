#pragma once
#include "DatabaseConnector.hpp"
#include <string>
#include <vector>
#include <map>

namespace SincroPro {

class APIConnector : public DatabaseConnector {
public:
    APIConnector() = default;
    ~APIConnector() override = default;

    bool connect(const std::string& connectionStringOrUrl) override;
    void disconnect() override;
    bool isConnected() const override;

    std::vector<std::string> getTables() override;
    std::vector<std::pair<std::string, std::string>> getColumns(const std::string& tableName) override;
    
    std::vector<std::map<std::string, std::string>> executeQuery(const std::string& queryOrEndpoint) override;
    std::string executeInsert(const std::string& tableNameOrEndpoint, const std::map<std::string, std::string>& data) override;
    bool executeUpdate(const std::string& tableNameOrEndpoint, const std::map<std::string, std::string>& data, const std::string& keyColumn, const std::string& keyValue) override;

private:
    std::string m_baseUrl;
    bool m_connected = false;
    std::map<std::string, std::string> m_headers;
    
    // Internal cache of schema
    std::vector<std::string> m_tables;
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> m_columns;

    void fetchSchema();
};

} // namespace SincroPro
