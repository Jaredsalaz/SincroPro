#include "APIConnector.hpp"
#include "HttpClient.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <algorithm>

using json = nlohmann::json;

namespace SincroPro {

bool APIConnector::connect(const std::string& connectionStringOrUrl) {
    // Expected format: http://api.server.com/api or similar base URL
    m_baseUrl = connectionStringOrUrl;
    if (m_baseUrl.back() == '/') {
        m_baseUrl.pop_back();
    }

    // Set standard headers
    m_headers["Content-Type"] = "application/json";
    m_headers["Accept"] = "application/json";

    m_connected = true;
    
    // Fetch schema from endpoints
    fetchSchema();
    
    return m_connected;
}

void APIConnector::disconnect() {
    m_connected = false;
    m_tables.clear();
    m_columns.clear();
}

bool APIConnector::isConnected() const {
    return m_connected;
}

std::vector<std::string> SincroPro::APIConnector::getTables() {
    return m_tables;
}

std::vector<std::pair<std::string, std::string>> APIConnector::getColumns(const std::string& tableName) {
    auto it = m_columns.find(tableName);
    if (it != m_columns.end()) {
        return it->second;
    }
    return {};
}

std::vector<std::map<std::string, std::string>> APIConnector::executeQuery(const std::string& queryOrEndpoint) {
    if (!m_connected) return {};

    std::string endpoint = queryOrEndpoint;
    std::string lowerQuery = queryOrEndpoint;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
    size_t selectPos = lowerQuery.find("select * from ");
    if (selectPos != std::string::npos) {
        endpoint = queryOrEndpoint.substr(selectPos + 14);
        endpoint.erase(0, endpoint.find_first_not_of(" \t"));
        size_t endPos = endpoint.find_last_not_of(" \t;");
        if (endPos != std::string::npos) {
            endpoint = endpoint.substr(0, endPos + 1);
        }
        if (!endpoint.empty() && endpoint.front() == '`' && endpoint.back() == '`') {
            endpoint = endpoint.substr(1, endpoint.length() - 2);
        }
        if (!endpoint.empty() && endpoint.front() == '[' && endpoint.back() == ']') {
            endpoint = endpoint.substr(1, endpoint.length() - 2);
        }
    }

    std::string url;
    if (endpoint.rfind("http", 0) == 0) {
        url = endpoint;
    } else {
        url = m_baseUrl + "/" + endpoint;
    }

    std::string response = HttpClient::get(url, m_headers);
    std::vector<std::map<std::string, std::string>> results;

    try {
        if (!response.empty()) {
            auto j = json::parse(response);
            if (j.is_array()) {
                for (const auto& row : j) {
                    std::map<std::string, std::string> record;
                    for (auto it = row.begin(); it != row.end(); ++it) {
                        if (it.value().is_null()) {
                            record[it.key()] = "";
                        } else if (it.value().is_string()) {
                            record[it.key()] = it.value().get<std::string>();
                        } else {
                            // Convert other types to string representation
                            record[it.key()] = it.value().dump();
                        }
                    }
                    results.push_back(record);
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "APIConnector: Failed to parse query response: " << e.what() << "\nResponse was: " << response << std::endl;
    }

    return results;
}

std::string APIConnector::executeInsert(const std::string& tableNameOrEndpoint, const std::map<std::string, std::string>& data) {
    if (!m_connected) return "";

    std::string url = m_baseUrl + "/" + tableNameOrEndpoint;
    
    // Construct JSON payload
    json payload = json::object();
    for (const auto& pair : data) {
        payload[pair.first] = pair.second;
    }

    std::string body = payload.dump();
    std::string response = HttpClient::post(url, body, m_headers);

    // Usually APIs return the created object containing the generated ID/GUID, or the ID directly.
    try {
        if (!response.empty()) {
            auto j = json::parse(response);
            if (j.is_object() && j.contains("error")) {
                std::cerr << "APIConnector: Insert failed with error: " << j["error"].dump() << std::endl;
                return "";
            }
            if (j.is_object() && j.contains("id")) {
                if (j["id"].is_string()) return j["id"].get<std::string>();
                return j["id"].dump();
            } else if (j.is_object() && j.contains("guid")) {
                return j["guid"].get<std::string>();
            } else if (j.is_string()) {
                return j.get<std::string>();
            }
            return "";
        }
    } catch (...) {
        return "";
    }

    return "";
}

bool APIConnector::executeUpdate(const std::string& tableNameOrEndpoint, const std::map<std::string, std::string>& data, const std::string& keyColumn, const std::string& keyValue) {
    if (!m_connected) return false;

    // Send PUT request to endpoint /table/id
    std::string url = m_baseUrl + "/" + tableNameOrEndpoint + "/" + keyValue;

    json payload = json::object();
    for (const auto& pair : data) {
        payload[pair.first] = pair.second;
    }

    std::string body = payload.dump();
    std::string response = HttpClient::put(url, body, m_headers);

    try {
        if (!response.empty()) {
            auto j = json::parse(response);
            if (j.is_object() && j.contains("error")) {
                return false;
            }
            return true;
        }
    } catch (...) {}

    return !response.empty();
}

void APIConnector::fetchSchema() {
    std::string schemaUrl = m_baseUrl + "/schema";
    std::string response = HttpClient::get(schemaUrl, m_headers);

    if (response.empty()) {
        std::cerr << "APIConnector: Failed to fetch schema from: " << schemaUrl << std::endl;
        
        // Provide mock schema tables if empty for demonstration/fallback
        m_tables = {"usuarios", "productos", "ventas"};
        m_columns["usuarios"] = {{"id", "string"}, {"nombre", "string"}, {"correo", "string"}};
        m_columns["productos"] = {{"id", "string"}, {"descripcion", "string"}, {"precio", "float"}};
        m_columns["ventas"] = {{"id", "string"}, {"usuario_id", "string"}, {"producto_id", "string"}, {"cantidad", "int"}};
        return;
    }

    try {
        auto j = json::parse(response);
        if (j.is_array()) {
            for (const auto& tableObj : j) {
                if (tableObj.contains("table") && tableObj.contains("columns")) {
                    std::string tableName = tableObj["table"];
                    m_tables.push_back(tableName);
                    
                    std::vector<std::pair<std::string, std::string>> cols;
                    for (const auto& colObj : tableObj["columns"]) {
                        if (colObj.contains("name") && colObj.contains("type")) {
                            cols.push_back({colObj["name"], colObj["type"]});
                        }
                    }
                    m_columns[tableName] = cols;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "APIConnector: Error parsing schema JSON: " << e.what() << std::endl;
        // Fallback mockup
        m_tables = {"usuarios", "productos", "ventas"};
        m_columns["usuarios"] = {{"id", "string"}, {"nombre", "string"}, {"correo", "string"}};
        m_columns["productos"] = {{"id", "string"}, {"descripcion", "string"}, {"precio", "float"}};
        m_columns["ventas"] = {{"id", "string"}, {"usuario_id", "string"}, {"producto_id", "string"}, {"cantidad", "int"}};
    }
}

} // namespace SincroPro
