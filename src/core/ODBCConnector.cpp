#include "ODBCConnector.hpp"
#include <iostream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

namespace SincroPro {

ODBCConnector::ODBCConnector() {
}

ODBCConnector::~ODBCConnector() {
    disconnect();
}

bool ODBCConnector::connect(const std::string& connectionStringOrUrl) {
    if (connectionStringOrUrl == "MOCK_DB_A") {
        m_connected = true;
        std::cout << "ODBCConnector: Connected in MOCK mode." << std::endl;
        return true;
    }

    SQLRETURN ret;

    // Allocate Environment Handle
    ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, (SQLHENV*)&m_env);
    if (!SQL_SUCCEEDED(ret)) {
        std::cerr << "ODBC: Failed to allocate environment handle." << std::endl;
        return false;
    }

    // Set ODBC Version
    ret = SQLSetEnvAttr((SQLHENV)m_env, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);
    if (!SQL_SUCCEEDED(ret)) {
        std::cerr << "ODBC: Failed to set ODBC version." << std::endl;
        SQLFreeHandle(SQL_HANDLE_ENV, (SQLHENV)m_env);
        m_env = nullptr;
        return false;
    }

    // Allocate Connection Handle
    ret = SQLAllocHandle(SQL_HANDLE_DBC, (SQLHENV)m_env, (SQLHDBC*)&m_dbc);
    if (!SQL_SUCCEEDED(ret)) {
        std::cerr << "ODBC: Failed to allocate connection handle." << std::endl;
        SQLFreeHandle(SQL_HANDLE_ENV, (SQLHENV)m_env);
        m_env = nullptr;
        return false;
    }

    // Connect
    SQLCHAR outConnStr[1024];
    SQLSMALLINT outConnStrLen;
    ret = SQLDriverConnect((SQLHDBC)m_dbc, NULL, 
                           (SQLCHAR*)connectionStringOrUrl.c_str(), SQL_NTS,
                           outConnStr, sizeof(outConnStr), &outConnStrLen, 
                           SQL_DRIVER_NOPROMPT);

    if (SQL_SUCCEEDED(ret)) {
        m_connected = true;
        return true;
    } else {
        std::cerr << "ODBC Connection Error: " << getLastError(m_dbc, SQL_HANDLE_DBC) << std::endl;
        disconnect();
        return false;
    }
}

void ODBCConnector::disconnect() {
    if (m_dbc) {
        if (m_connected) {
            SQLDisconnect((SQLHDBC)m_dbc);
        }
        SQLFreeHandle(SQL_HANDLE_DBC, (SQLHDBC)m_dbc);
        m_dbc = nullptr;
    }
    if (m_env) {
        SQLFreeHandle(SQL_HANDLE_ENV, (SQLHENV)m_env);
        m_env = nullptr;
    }
    m_connected = false;
}

bool ODBCConnector::isConnected() const {
    return m_connected;
}

std::vector<std::string> ODBCConnector::getTables() {
    if (!m_connected) return {};

    if (m_dbc == nullptr && m_connected) { // MOCK mode
        return {"clientes", "inventario", "transacciones"};
    }

    std::vector<std::string> tables;
    SQLHSTMT hstmt = nullptr;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, (SQLHDBC)m_dbc, &hstmt);
    if (!SQL_SUCCEEDED(ret)) return {};

    // Filter to get only user tables
    ret = SQLTables(hstmt, NULL, 0, NULL, 0, NULL, 0, (SQLCHAR*)"TABLE", SQL_NTS);
    if (SQL_SUCCEEDED(ret)) {
        SQLCHAR tableName[256];
        SQLLEN lenTableName;
        SQLBindCol(hstmt, 3, SQL_C_CHAR, tableName, sizeof(tableName), &lenTableName);

        while (SQL_SUCCEEDED(SQLFetch(hstmt))) {
            tables.push_back((char*)tableName);
        }
    } else {
        std::cerr << "ODBC: SQLTables error: " << getLastError(hstmt, SQL_HANDLE_STMT) << std::endl;
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return tables;
}

std::vector<std::pair<std::string, std::string>> ODBCConnector::getColumns(const std::string& tableName) {
    if (!m_connected) return {};

    if (m_dbc == nullptr && m_connected) { // MOCK mode
        if (tableName == "clientes") {
            return {{"id_cliente", "int"}, {"nombre", "string"}, {"email", "string"}, {"fecha_registro", "date"}};
        } else if (tableName == "inventario") {
            return {{"id_item", "int"}, {"descripcion", "string"}, {"stock", "int"}, {"precio_venta", "float"}};
        } else if (tableName == "transacciones") {
            return {{"id_trx", "int"}, {"cliente_id", "int"}, {"item_id", "int"}, {"cantidad", "int"}, {"total", "float"}};
        }
        return {};
    }

    std::vector<std::pair<std::string, std::string>> columns;
    SQLHSTMT hstmt = nullptr;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, (SQLHDBC)m_dbc, &hstmt);
    if (!SQL_SUCCEEDED(ret)) return {};

    // Use SQLColumns to fetch metadata
    ret = SQLColumns(hstmt, NULL, 0, NULL, 0, (SQLCHAR*)tableName.c_str(), SQL_NTS, NULL, 0);
    if (SQL_SUCCEEDED(ret)) {
        SQLCHAR colName[256];
        SQLCHAR colType[128];
        SQLLEN lenColName, lenColType;
        
        SQLBindCol(hstmt, 4, SQL_C_CHAR, colName, sizeof(colName), &lenColName);
        SQLBindCol(hstmt, 6, SQL_C_CHAR, colType, sizeof(colType), &lenColType);

        while (SQL_SUCCEEDED(SQLFetch(hstmt))) {
            columns.push_back({(char*)colName, (char*)colType});
        }
    } else {
        std::cerr << "ODBC: SQLColumns error: " << getLastError(hstmt, SQL_HANDLE_STMT) << std::endl;
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return columns;
}

std::vector<std::map<std::string, std::string>> ODBCConnector::executeQuery(const std::string& queryOrEndpoint) {
    if (!m_connected) return {};

    if (m_dbc == nullptr && m_connected) { // MOCK mode
        // Return dummy rows for testing
        std::vector<std::map<std::string, std::string>> mockRows;
        if (queryOrEndpoint.find("clientes") != std::string::npos) {
            mockRows.push_back({{"id_cliente", "1"}, {"nombre", "Carlos Perez"}, {"email", "carlos@example.com"}, {"fecha_registro", "2026-01-10"}});
            mockRows.push_back({{"id_cliente", "2"}, {"nombre", "Maria Lopez"}, {"email", "maria@example.com"}, {"fecha_registro", "2026-02-15"}});
        } else if (queryOrEndpoint.find("inventario") != std::string::npos) {
            mockRows.push_back({{"id_item", "101"}, {"descripcion", "Bateria Motocicleta"}, {"stock", "45"}, {"precio_venta", "850.50"}});
            mockRows.push_back({{"id_item", "102"}, {"descripcion", "Casco Protector"}, {"stock", "20"}, {"precio_venta", "1200.00"}});
        } else if (queryOrEndpoint.find("transacciones") != std::string::npos) {
            mockRows.push_back({{"id_trx", "5001"}, {"cliente_id", "1"}, {"item_id", "101"}, {"cantidad", "1"}, {"total", "850.50"}});
            mockRows.push_back({{"id_trx", "5002"}, {"cliente_id", "2"}, {"item_id", "102"}, {"cantidad", "2"}, {"total", "2400.00"}});
        }
        return mockRows;
    }

    std::vector<std::map<std::string, std::string>> results;
    SQLHSTMT hstmt = nullptr;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, (SQLHDBC)m_dbc, &hstmt);
    if (!SQL_SUCCEEDED(ret)) return {};

    ret = SQLExecDirect(hstmt, (SQLCHAR*)queryOrEndpoint.c_str(), SQL_NTS);
    if (SQL_SUCCEEDED(ret)) {
        SQLSMALLINT numCols = 0;
        SQLNumResultCols(hstmt, &numCols);

        std::vector<std::string> colNames;
        for (SQLSMALLINT i = 1; i <= numCols; ++i) {
            SQLCHAR colName[256];
            SQLSMALLINT colNameLen;
            SQLColAttribute(hstmt, i, SQL_DESC_NAME, colName, sizeof(colName), &colNameLen, nullptr);
            colNames.push_back((char*)colName);
        }

        while (SQL_SUCCEEDED(SQLFetch(hstmt))) {
            std::map<std::string, std::string> row;
            for (SQLSMALLINT i = 1; i <= numCols; ++i) {
                SQLCHAR colValue[512];
                SQLLEN colValueLen;
                SQLGetData(hstmt, i, SQL_C_CHAR, colValue, sizeof(colValue), &colValueLen);
                if (colValueLen == SQL_NULL_DATA) {
                    row[colNames[i - 1]] = "";
                } else {
                    row[colNames[i - 1]] = (char*)colValue;
                }
            }
            results.push_back(row);
        }
    } else {
        std::cerr << "ODBC Exec error: " << getLastError(hstmt, SQL_HANDLE_STMT) << std::endl;
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return results;
}

std::string ODBCConnector::executeInsert(const std::string& tableNameOrEndpoint, const std::map<std::string, std::string>& data) {
    if (!m_connected) return "";

    std::stringstream cols, vals;
    bool first = true;
    for (const auto& pair : data) {
        if (!first) {
            cols << ", ";
            vals << ", ";
        }
        cols << pair.first;
        vals << "'" << pair.second << "'"; // Basic SQL escape wrapper
        first = false;
    }

    std::stringstream query;
    query << "INSERT INTO " << tableNameOrEndpoint << " (" << cols.str() << ") VALUES (" << vals.str() << ")";

    if (m_dbc == nullptr && m_connected) { // MOCK mode
        std::cout << "ODBC Mock Insert: " << query.str() << std::endl;
        return "MOCK_INSERTED_ID_" + std::to_string(rand() % 1000);
    }

    SQLHSTMT hstmt = nullptr;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, (SQLHDBC)m_dbc, &hstmt);
    if (!SQL_SUCCEEDED(ret)) return "";

    ret = SQLExecDirect(hstmt, (SQLCHAR*)query.str().c_str(), SQL_NTS);
    std::string newId = "";
    if (SQL_SUCCEEDED(ret)) {
        // Try fetching last identity value
        SQLHSTMT hstmtId = nullptr;
        SQLAllocHandle(SQL_HANDLE_STMT, (SQLHDBC)m_dbc, &hstmtId);
        // This is SQL Server syntax. For Mysql use SELECT LAST_INSERT_ID()
        SQLCHAR lastId[64];
        SQLLEN len;
        if (SQL_SUCCEEDED(SQLExecDirect(hstmtId, (SQLCHAR*)"SELECT SCOPE_IDENTITY()", SQL_NTS)) &&
            SQL_SUCCEEDED(SQLFetch(hstmtId))) {
            SQLGetData(hstmtId, 1, SQL_C_CHAR, lastId, sizeof(lastId), &len);
            if (len != SQL_NULL_DATA) newId = (char*)lastId;
        }
        SQLFreeHandle(SQL_HANDLE_STMT, hstmtId);
    } else {
        std::cerr << "ODBC Insert error: " << getLastError(hstmt, SQL_HANDLE_STMT) << std::endl;
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return newId;
}

bool ODBCConnector::executeUpdate(const std::string& tableNameOrEndpoint, const std::map<std::string, std::string>& data, const std::string& keyColumn, const std::string& keyValue) {
    if (!m_connected) return false;

    std::stringstream sets;
    bool first = true;
    for (const auto& pair : data) {
        if (pair.first == keyColumn) continue;
        if (!first) sets << ", ";
        sets << pair.first << " = '" << pair.second << "'";
        first = false;
    }

    std::stringstream query;
    query << "UPDATE " << tableNameOrEndpoint << " SET " << sets.str() << " WHERE " << keyColumn << " = '" << keyValue << "'";

    if (m_dbc == nullptr && m_connected) { // MOCK mode
        std::cout << "ODBC Mock Update: " << query.str() << std::endl;
        return true;
    }

    SQLHSTMT hstmt = nullptr;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, (SQLHDBC)m_dbc, &hstmt);
    if (!SQL_SUCCEEDED(ret)) return false;

    ret = SQLExecDirect(hstmt, (SQLCHAR*)query.str().c_str(), SQL_NTS);
    bool success = SQL_SUCCEEDED(ret);
    if (!success) {
         std::cerr << "ODBC Update error: " << getLastError(hstmt, SQL_HANDLE_STMT) << std::endl;
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return success;
}

std::string ODBCConnector::getLastError(void* handle, int handleType) {
    SQLCHAR sqlState[6], message[SQL_MAX_MESSAGE_LENGTH];
    SQLINTEGER nativeError;
    SQLSMALLINT msgLen;
    
    SQLGetDiagRec(handleType, handle, 1, sqlState, &nativeError, message, sizeof(message), &msgLen);
    
    std::stringstream ss;
    ss << "State: " << sqlState << ", Native Error: " << nativeError << ", Msg: " << message;
    return ss.str();
}

} // namespace SincroPro
