package main

import (
	"bytes"
	"database/sql"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	_ "github.com/go-sql-driver/mysql"
	"github.com/google/uuid"
	_ "github.com/microsoft/go-mssqldb"
)

// Global Handles
var (
	mysqlDB *sql.DB
	mssqlDB *sql.DB

	schemaCacheLock sync.RWMutex
	schemaCacheData []TableSchema
	schemaCacheTime time.Time

	// ID Translation Tracker in memory for Go Daemon
	idTrackerLock sync.RWMutex
	idTrackerMap  = make(map[string]string) // key: "session:translateViaTable:sourceId" -> targetId
)

// Data Structures
type ColumnInfo struct {
	Name       string `json:"name"`
	Type       string `json:"type"`
	RawType    string `json:"raw_type"`
	IsNullable string `json:"is_nullable"`
	HasDefault bool   `json:"has_default"`
}

type TableSchema struct {
	Table    string       `json:"table"`
	RawTable string       `json:"raw_table"`
	Schema   string       `json:"schema"`
	Columns  []ColumnInfo `json:"columns"`
}

// Config file session structs
type ColumnMapping struct {
	ColA              string `json:"colA"`
	ColB              string `json:"colB"`
	IsKey             bool   `json:"isKey"`
	TranslateViaTable string `json:"translateViaTable"`
}

type TableMappingConfig struct {
	TableA      string          `json:"tableA"`
	TableB      string          `json:"tableB"`
	Direction   string          `json:"direction"`
	IDGenerator bool            `json:"idGenerator"`
	Columns     []ColumnMapping `json:"columns"`
}

type SyncSessionConfig struct {
	Name            string               `json:"name"`
	DbAConnStr      string               `json:"dbA_connStr"`
	DbBURL          string               `json:"dbB_url"`
	IntervalSeconds int                  `json:"intervalSeconds"`
	Active          bool                 `json:"active"`
	Mappings        []TableMappingConfig `json:"mappings"`
}

type SessionsConfigFile struct {
	Sessions []SyncSessionConfig `json:"sessions"`
}

func getEnv(key, fallback string) string {
	if val, ok := os.LookupEnv(key); ok && val != "" {
		return val
	}
	return fallback
}

func mapType(dataType string) string {
	dt := strings.ToLower(dataType)
	if strings.Contains(dt, "int") {
		return "int"
	}
	if strings.Contains(dt, "float") || strings.Contains(dt, "double") ||
		strings.Contains(dt, "decimal") || strings.Contains(dt, "numeric") || strings.Contains(dt, "real") {
		return "float"
	}
	return "string"
}

func main() {
	log.Println("==================================================")
	log.Println("🚀 Starting MySonda Go High-Performance API Server & Replix Daemon 24/7")
	log.Println("==================================================")

	// 1. Initialize MySQL Connection Pool (Configurable via MYSQL_CONN_STR env var)
	mysqlConnStr := getEnv("MYSQL_CONN_STR", "root:password@tcp(127.0.0.1:3306)/your_database_a?parseTime=true&charset=utf8")
	var err error
	mysqlDB, err = sql.Open("mysql", mysqlConnStr)
	if err != nil {
		log.Printf("⚠️ MySQL Connection Notice: %v", err)
	}
	mysqlDB.SetMaxOpenConns(50)
	mysqlDB.SetMaxIdleConns(25)
	mysqlDB.SetConnMaxLifetime(5 * time.Minute)
	log.Println("✅ MySQL Connection Pool initialized")

	// 2. Initialize SQL Server Connection Pool (Configurable via MSSQL_CONN_STR env var)
	mssqlConnStr := getEnv("MSSQL_CONN_STR", "server=localhost;user id=sa;password=your_password;database=your_database_b;encrypt=disable;TrustServerCertificate=true;")
	mssqlDB, err = sql.Open("mssql", mssqlConnStr)
	if err != nil {
		log.Printf("⚠️ SQL Server Connection Notice: %v", err)
	}
	mssqlDB.SetMaxOpenConns(50)
	mssqlDB.SetMaxIdleConns(25)
	mssqlDB.SetConnMaxLifetime(5 * time.Minute)
	log.Println("✅ SQL Server Connection Pool initialized")

	// 3. Load or Warm Schema Cache & ID Mappings
	loadSchemaCacheFromDisk()
	loadIDMappingsFromDisk()

	// 4. Start Background Replix Replication Daemon
	go runReplixDaemon()

	// 5. Start HTTP Servers in parallel goroutines
	var wg sync.WaitGroup
	wg.Add(2)

	go func() {
		defer wg.Done()
		mux := http.NewServeMux()
		mux.HandleFunc("/api/mysondav1/schema", handleV1Schema)
		mux.HandleFunc("/api/mysondav1/", handleV1Table)

		log.Println("⚡ [Mysonda V1 API - MySQL] Listening on http://localhost:5001")
		if err := http.ListenAndServe(":5001", mux); err != nil {
			log.Fatalf("Error in V1 server: %v", err)
		}
	}()

	go func() {
		defer wg.Done()
		mux := http.NewServeMux()
		mux.HandleFunc("/api/mysondav2/schema", handleV2Schema)
		mux.HandleFunc("/api/mysondav2/batch/", handleV2Batch)
		mux.HandleFunc("/api/mysondav2/", handleV2Table)

		log.Println("⚡ [Mysonda V2 API - SQL Server] Listening on http://localhost:5002")
		if err := http.ListenAndServe(":5002", mux); err != nil {
			log.Fatalf("Error in V2 server: %v", err)
		}
	}()

	wg.Wait()
}

// -----------------------------------------------------------------------------
// Background Replix Replication Daemon (Runs 24/7 even if GUI is closed)
// -----------------------------------------------------------------------------
func findSessionsConfigPath() string {
	cwd, _ := os.Getwd()
	candidates := []string{
		filepath.Join(cwd, "config.json"),
		filepath.Join(cwd, "sessions.json"),
		filepath.Join(cwd, "..", "build", "bin", "Release", "config.json"),
		filepath.Join(cwd, "..", "build", "bin", "Release", "sessions.json"),
		filepath.Join(cwd, "..", "..", "build", "bin", "Release", "config.json"),
		filepath.Join(cwd, "..", "..", "build", "bin", "Release", "sessions.json"),
		`C:\Users\MSI\Documents\SincroPro\build\bin\Release\config.json`,
		`C:\Users\MSI\Documents\SincroPro\build\bin\Release\sessions.json`,
	}
	for _, p := range candidates {
		if _, err := os.Stat(p); err == nil {
			return p
		}
	}
	return ""
}

func runReplixDaemon() {
	log.Println("🔄 [Replix Daemon 24/7] Background replication worker started.")
	lastSyncMap := make(map[string]time.Time)

	for {
		time.Sleep(2 * time.Second)

		configPath := findSessionsConfigPath()
		if configPath == "" {
			continue
		}

		data, err := os.ReadFile(configPath)
		if err != nil {
			continue
		}

		var cfg SessionsConfigFile
		if err := json.Unmarshal(data, &cfg); err != nil {
			continue
		}

		for _, s := range cfg.Sessions {
			if !s.Active {
				continue
			}

			intervalSecs := s.IntervalSeconds
			if intervalSecs <= 0 {
				intervalSecs = 5
			}

			lastTime := lastSyncMap[s.Name]
			if time.Since(lastTime) >= time.Duration(intervalSecs)*time.Second {
				lastSyncMap[s.Name] = time.Now()
				executeDaemonSyncSession(s)
			}
		}
	}
}

func executeDaemonSyncSession(session SyncSessionConfig) {
	log.Printf("🔄 [Replix Daemon 24/7] Session [%s]: Running synchronization pass...", session.Name)

	for _, tm := range session.Mappings {
		syncDaemonTableMapping(session.Name, tm)
	}
}

func idMappingsFilePath() string {
	dir, _ := os.Getwd()
	return filepath.Join(dir, "id_mappings.json")
}

func loadIDMappingsFromDisk() {
	idTrackerLock.Lock()
	defer idTrackerLock.Unlock()

	data, err := os.ReadFile(idMappingsFilePath())
	if err != nil {
		return
	}
	json.Unmarshal(data, &idTrackerMap)
	log.Printf("📦 Loaded %d ID mappings from id_mappings.json", len(idTrackerMap))
}

func saveIDMappingsToDisk() {
	idTrackerLock.RLock()
	defer idTrackerLock.RUnlock()

	data, err := json.MarshalIndent(idTrackerMap, "", "  ")
	if err != nil {
		return
	}
	os.WriteFile(idMappingsFilePath(), data, 0644)
}

func syncDaemonTableMapping(sessionName string, tm TableMappingConfig) {
	// 1. Fetch rows from Source (MySQL - V1 API)
	urlA := fmt.Sprintf("http://localhost:5001/api/mysondav1/%s", tm.TableA)
	resp, err := http.Get(urlA)
	if err != nil {
		log.Printf("⚠️ [Replix Daemon] Failed to fetch source data for %s: %v", tm.TableA, err)
		return
	}
	defer resp.Body.Close()

	bodyBytes, _ := io.ReadAll(resp.Body)
	var rowsA []map[string]interface{}
	if err := json.Unmarshal(bodyBytes, &rowsA); err != nil {
		return
	}

	insertedCount := 0
	updatedCount := 0

	for _, row := range rowsA {
		payload := make(map[string]interface{})
		var pkSourceValue string
		missingFK := false

		// Build target payload and handle FK translations
		for _, col := range tm.Columns {
			val := row[col.ColA]
			valStr := ""
			if val != nil {
				valStr = fmt.Sprintf("%v", val)
			}

			if col.IsKey {
				pkSourceValue = valStr
			}

			// Foreign Key Translation
			if col.TranslateViaTable != "" {
				key := fmt.Sprintf("%s:%s:%s", sessionName, col.TranslateViaTable, valStr)
				idTrackerLock.RLock()
				targetFK, found := idTrackerMap[key]
				idTrackerLock.RUnlock()

				if !found || targetFK == "" || targetFK == "0" {
					// Missing parent reference -> skip entire ROW
					missingFK = true
					break
				}
				payload[col.ColB] = targetFK
			} else {
				payload[col.ColB] = val
			}
		}

		if missingFK || pkSourceValue == "" {
			continue
		}

		// Check if we already have a mapped target ID
		mapReqKey := fmt.Sprintf("%s:%s:%s", sessionName, tm.TableA, pkSourceValue)
		idTrackerLock.RLock()
		existingTargetID, exists := idTrackerMap[mapReqKey]
		idTrackerLock.RUnlock()

		if exists && existingTargetID != "" {
			// Execute PUT (Update)
			urlPUT := fmt.Sprintf("http://localhost:5002/api/mysondav2/%s/%s", tm.TableB, existingTargetID)
			putBody, _ := json.Marshal(payload)
			req, _ := http.NewRequest("PUT", urlPUT, bytes.NewBuffer(putBody))
			req.Header.Set("Content-Type", "application/json")

			client := &http.Client{Timeout: 10 * time.Second}
			respPUT, err := client.Do(req)
			if err == nil {
				respPUT.Body.Close()
				updatedCount++
			}
		} else {
			// Execute POST (Insert)
			urlPOST := fmt.Sprintf("http://localhost:5002/api/mysondav2/%s", tm.TableB)
			postBody, _ := json.Marshal(payload)

			client := &http.Client{Timeout: 10 * time.Second}
			respPOST, err := client.Post(urlPOST, "application/json", bytes.NewBuffer(postBody))
			if err == nil {
				var resData map[string]string
				json.NewDecoder(respPOST.Body).Decode(&resData)
				respPOST.Body.Close()

				if targetID, ok := resData["id"]; ok && targetID != "" {
					idTrackerLock.Lock()
					idTrackerMap[mapReqKey] = targetID
					idTrackerLock.Unlock()
					saveIDMappingsToDisk()
					insertedCount++
				}
			}
		}
	}

	log.Printf("✨ [Replix Daemon] Stats for %s -> %s: %d inserted, %d updated.", tm.TableA, tm.TableB, insertedCount, updatedCount)
}

// -----------------------------------------------------------------------------
// Schema Caching
// -----------------------------------------------------------------------------
func cacheFilePath() string {
	dir, _ := os.Getwd()
	return filepath.Join(dir, "schema_cache.json")
}

func loadSchemaCacheFromDisk() bool {
	data, err := os.ReadFile(cacheFilePath())
	if err != nil {
		return false
	}
	var schema []TableSchema
	if err := json.Unmarshal(data, &schema); err != nil {
		return false
	}
	schemaCacheLock.Lock()
	schemaCacheData = schema
	schemaCacheTime = time.Now()
	schemaCacheLock.Unlock()
	log.Printf("📦 Loaded %d tables from schema_cache.json (Instant Boot)", len(schema))
	return true
}

func saveSchemaCacheToDisk(schema []TableSchema) {
	data, err := json.MarshalIndent(schema, "", "  ")
	if err != nil {
		return
	}
	os.WriteFile(cacheFilePath(), data, 0644)
}

func fetchV2Schema(forceRefresh bool) ([]TableSchema, error) {
	schemaCacheLock.RLock()
	if !forceRefresh && len(schemaCacheData) > 0 && time.Since(schemaCacheTime) < 10*time.Minute {
		defer schemaCacheLock.RUnlock()
		return schemaCacheData, nil
	}
	schemaCacheLock.RUnlock()

	catalog := getEnv("MSSQL_DB_NAME", "MySonda_Sincro")
	query := fmt.Sprintf(`
		SELECT TABLE_SCHEMA, TABLE_NAME, COLUMN_NAME, DATA_TYPE, IS_NULLABLE, COLUMN_DEFAULT 
		FROM INFORMATION_SCHEMA.COLUMNS 
		WHERE TABLE_CATALOG = '%s'
		ORDER BY TABLE_SCHEMA, TABLE_NAME, ORDINAL_POSITION
	`, catalog)
	rows, err := mssqlDB.Query(query)
	if err != nil {
		if loadSchemaCacheFromDisk() {
			return schemaCacheData, nil
		}
		return nil, err
	}
	defer rows.Close()

	tablesMap := make(map[string]*TableSchema)
	var order []string

	for rows.Next() {
		var schemaName, tableName, colName, dataType, isNullable string
		var colDefault sql.NullString

		if err := rows.Scan(&schemaName, &tableName, &colName, &dataType, &isNullable, &colDefault); err != nil {
			continue
		}

		key := tableName
		if _, exists := tablesMap[key]; !exists {
			display := tableName
			if schemaName != "" && !strings.EqualFold(schemaName, "dbo") {
				display = schemaName + "." + tableName
			}
			tablesMap[key] = &TableSchema{
				Table:    display,
				RawTable: tableName,
				Schema:   schemaName,
				Columns:  []ColumnInfo{},
			}
			order = append(order, key)
		}

		tablesMap[key].Columns = append(tablesMap[key].Columns, ColumnInfo{
			Name:       colName,
			Type:       mapType(dataType),
			RawType:    strings.ToLower(dataType),
			IsNullable: isNullable,
			HasDefault: colDefault.Valid,
		})
	}

	var schemaList []TableSchema
	for _, key := range order {
		schemaList = append(schemaList, *tablesMap[key])
	}

	schemaCacheLock.Lock()
	schemaCacheData = schemaList
	schemaCacheTime = time.Now()
	schemaCacheLock.Unlock()

	saveSchemaCacheToDisk(schemaList)
	return schemaList, nil
}

func getV2PrimaryKey(tableName string) string {
	schemaList, _ := fetchV2Schema(false)
	rawTable := tableName
	schemaName := ""
	if strings.Contains(tableName, ".") {
		parts := strings.SplitN(tableName, ".", 2)
		schemaName = parts[0]
		rawTable = parts[1]
	}

	catalog := getEnv("MSSQL_DB_NAME", "MySonda_Sincro")
	var query string
	var args []interface{}
	if schemaName != "" {
		query = fmt.Sprintf(`
			SELECT COLUMN_NAME 
			FROM INFORMATION_SCHEMA.KEY_COLUMN_USAGE 
			WHERE TABLE_CATALOG = '%s' AND TABLE_SCHEMA = ? AND TABLE_NAME = ? AND CONSTRAINT_NAME IN (
				SELECT CONSTRAINT_NAME 
				FROM INFORMATION_SCHEMA.TABLE_CONSTRAINTS 
				WHERE CONSTRAINT_TYPE = 'PRIMARY KEY' AND TABLE_CATALOG = '%s' AND TABLE_SCHEMA = ? AND TABLE_NAME = ?
			)
		`, catalog, catalog)
		args = []interface{}{schemaName, rawTable, schemaName, rawTable}
	} else {
		query = fmt.Sprintf(`
			SELECT COLUMN_NAME 
			FROM INFORMATION_SCHEMA.KEY_COLUMN_USAGE 
			WHERE TABLE_CATALOG = '%s' AND TABLE_NAME = ? AND CONSTRAINT_NAME IN (
				SELECT CONSTRAINT_NAME 
				FROM INFORMATION_SCHEMA.TABLE_CONSTRAINTS 
				WHERE CONSTRAINT_TYPE = 'PRIMARY KEY' AND TABLE_CATALOG = '%s' AND TABLE_NAME = ?
			)
		`, catalog, catalog)
		args = []interface{}{rawTable, rawTable}
	}

	var pk string
	err := mssqlDB.QueryRow(query, args...).Scan(&pk)
	if err == nil && pk != "" {
		return pk
	}

	for _, t := range schemaList {
		if t.Table == tableName || t.RawTable == rawTable {
			for _, col := range t.Columns {
				if strings.EqualFold(col.Name, "id") {
					return col.Name
				}
			}
		}
	}
	return "Id"
}

func getFullV2TableName(tableName string) string {
	if strings.Contains(tableName, ".") {
		parts := strings.SplitN(tableName, ".", 2)
		return fmt.Sprintf("[%s].[%s]", parts[0], parts[1])
	}
	schemaList, _ := fetchV2Schema(false)
	for _, t := range schemaList {
		if t.Table == tableName || t.RawTable == tableName {
			return fmt.Sprintf("[%s].[%s]", t.Schema, t.RawTable)
		}
	}
	return fmt.Sprintf("[%s]", tableName)
}

func findTableInfo(tableName string) *TableSchema {
	schemaList, _ := fetchV2Schema(false)
	for i := range schemaList {
		t := &schemaList[i]
		if t.Table == tableName || t.RawTable == tableName {
			return t
		}
		if strings.Contains(tableName, ".") {
			parts := strings.SplitN(tableName, ".", 2)
			if t.Schema == parts[0] && t.RawTable == parts[1] {
				return t
			}
		}
	}
	return nil
}

// -----------------------------------------------------------------------------
// Mysonda V1 Handlers (MySQL - 5001)
// -----------------------------------------------------------------------------
func handleV1Schema(w http.ResponseWriter, r *http.Request) {
	mysqlCatalog := getEnv("MYSQL_DB_NAME", "BaseDatos")
	rows, err := mysqlDB.Query(`
		SELECT TABLE_NAME, COLUMN_NAME, DATA_TYPE 
		FROM INFORMATION_SCHEMA.COLUMNS 
		WHERE TABLE_SCHEMA = ?
		ORDER BY TABLE_NAME, ORDINAL_POSITION
	`, mysqlCatalog)
	if err != nil {
		http.Error(w, err.Error(), 500)
		return
	}
	defer rows.Close()

	tablesMap := make(map[string][]ColumnInfo)
	var order []string

	for rows.Next() {
		var tableName, colName, dataType string
		if err := rows.Scan(&tableName, &colName, &dataType); err != nil {
			continue
		}
		if _, exists := tablesMap[tableName]; !exists {
			order = append(order, tableName)
		}
		tablesMap[tableName] = append(tablesMap[tableName], ColumnInfo{
			Name: colName,
			Type: mapType(dataType),
		})
	}

	type V1Schema struct {
		Table   string       `json:"table"`
		Columns []ColumnInfo `json:"columns"`
	}

	var schemaList []V1Schema
	for _, tName := range order {
		schemaList = append(schemaList, V1Schema{
			Table:   tName,
			Columns: tablesMap[tName],
		})
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(schemaList)
}

func handleV1Table(w http.ResponseWriter, r *http.Request) {
	path := strings.TrimPrefix(r.URL.Path, "/api/mysondav1/")
	parts := strings.Split(path, "/")
	tableName := parts[0]

	if r.Method == "GET" {
		rows, err := mysqlDB.Query(fmt.Sprintf("SELECT * FROM `%s`", tableName))
		if err != nil {
			http.Error(w, err.Error(), 500)
			return
		}
		defer rows.Close()

		cols, _ := rows.Columns()
		var results []map[string]interface{}

		for rows.Next() {
			vals := make([]interface{}, len(cols))
			valPtrs := make([]interface{}, len(cols))
			for i := range vals {
				valPtrs[i] = &vals[i]
			}
			rows.Scan(valPtrs...)

			record := make(map[string]interface{})
			for i, col := range cols {
				if vals[i] == nil {
					record[col] = nil
				} else if b, ok := vals[i].([]byte); ok {
					record[col] = string(b)
				} else {
					record[col] = vals[i]
				}
			}
			results = append(results, record)
		}

		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(results)
		return
	}

	w.WriteHeader(405)
}

// -----------------------------------------------------------------------------
// Mysonda V2 Handlers (SQL Server - 5002)
// -----------------------------------------------------------------------------
func handleV2Schema(w http.ResponseWriter, r *http.Request) {
	force := r.URL.Query().Get("refresh") == "true"
	schema, err := fetchV2Schema(force)
	if err != nil {
		http.Error(w, err.Error(), 500)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(schema)
}

func handleV2Table(w http.ResponseWriter, r *http.Request) {
	path := strings.TrimPrefix(r.URL.Path, "/api/mysondav2/")
	parts := strings.Split(path, "/")
	tableName := parts[0]
	fullTable := getFullV2TableName(tableName)

	switch r.Method {
	case "GET":
		rows, err := mssqlDB.Query(fmt.Sprintf("SELECT * FROM %s", fullTable))
		if err != nil {
			http.Error(w, err.Error(), 500)
			return
		}
		defer rows.Close()

		cols, _ := rows.Columns()
		var results []map[string]interface{}

		for rows.Next() {
			vals := make([]interface{}, len(cols))
			valPtrs := make([]interface{}, len(cols))
			for i := range vals {
				valPtrs[i] = &vals[i]
			}
			rows.Scan(valPtrs...)

			record := make(map[string]interface{})
			for i, col := range cols {
				if vals[i] == nil {
					record[col] = nil
				} else if b, ok := vals[i].([]byte); ok {
					record[col] = string(b)
				} else {
					record[col] = vals[i]
				}
			}
			results = append(results, record)
		}

		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(results)

	case "POST":
		var payload map[string]interface{}
		if err := json.NewDecoder(r.Body).Decode(&payload); err != nil {
			http.Error(w, err.Error(), 400)
			return
		}

		res, err := insertV2Data(tableName, payload)
		if err != nil {
			http.Error(w, err.Error(), 500)
			return
		}

		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(201)
		json.NewEncoder(w).Encode(res)

	case "PUT":
		if len(parts) < 2 {
			http.Error(w, "Missing row ID in path", 400)
			return
		}
		rowID := parts[1]

		var payload map[string]interface{}
		if err := json.NewDecoder(r.Body).Decode(&payload); err != nil {
			http.Error(w, err.Error(), 400)
			return
		}

		ok, err := updateV2Data(tableName, rowID, payload)
		if err != nil {
			http.Error(w, err.Error(), 500)
			return
		}

		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]interface{}{"success": ok})

	default:
		w.WriteHeader(405)
	}
}

func insertV2Data(tableName string, payload map[string]interface{}) (map[string]string, error) {
	fullTable := getFullV2TableName(tableName)
	tInfo := findTableInfo(tableName)
	pk := getV2PrimaryKey(tableName)

	filteredPayload := make(map[string]interface{})
	var generatedUUID string

	if tInfo != nil {
		for _, col := range tInfo.Columns {
			k := col.Name
			rawType := col.RawType

			if val, exists := payload[k]; exists && val != nil {
				if strings.Contains(rawType, "uniqueidentifier") {
					valStr := fmt.Sprintf("%v", val)
					if u, err := uuid.Parse(valStr); err == nil {
						filteredPayload[k] = u.String()
					} else if valStr != "" {
						filteredPayload[k] = uuid.NewSHA1(uuid.NameSpaceDNS, []byte(valStr)).String()
					} else {
						filteredPayload[k] = uuid.NewString()
					}
				} else {
					filteredPayload[k] = val
				}
			} else {
				if strings.EqualFold(k, pk) && strings.Contains(rawType, "uniqueidentifier") {
					generatedUUID = uuid.NewString()
					filteredPayload[k] = generatedUUID
				} else if col.IsNullable == "NO" && !col.HasDefault {
					if strings.Contains(rawType, "uniqueidentifier") {
						filteredPayload[k] = uuid.NewString()
					} else if strings.Contains(rawType, "int") || strings.Contains(rawType, "float") || strings.Contains(rawType, "decimal") {
						filteredPayload[k] = 0
					} else {
						filteredPayload[k] = "0"
					}
				}
			}
		}
	} else {
		filteredPayload = payload
	}

	if len(filteredPayload) == 0 {
		return map[string]string{"id": "ok"}, nil
	}

	var cols []string
	var placeholders []string
	var args []interface{}

	for k, v := range filteredPayload {
		cols = append(cols, fmt.Sprintf("[%s]", k))
		placeholders = append(placeholders, "?")
		args = append(args, v)
	}

	query := fmt.Sprintf("INSERT INTO %s (%s) VALUES (%s)", fullTable, strings.Join(cols, ", "), strings.Join(placeholders, ", "))
	_, err := mssqlDB.Exec(query, args...)
	if err != nil {
		return nil, err
	}

	finalID := generatedUUID
	if finalID == "" && pk != "" {
		if val, exists := filteredPayload[pk]; exists && val != nil {
			finalID = fmt.Sprintf("%v", val)
		}
	}
	if finalID == "" {
		finalID = "ok"
	}

	return map[string]string{"id": finalID}, nil
}

func updateV2Data(tableName, rowID string, payload map[string]interface{}) (bool, error) {
	fullTable := getFullV2TableName(tableName)
	tInfo := findTableInfo(tableName)
	pk := getV2PrimaryKey(tableName)

	validCols := make(map[string]string)
	if tInfo != nil {
		for _, col := range tInfo.Columns {
			validCols[col.Name] = col.RawType
		}
	}

	filteredPayload := make(map[string]interface{})
	for k, v := range payload {
		rawType, isValid := validCols[k]
		if (len(validCols) == 0 || isValid) && !strings.EqualFold(k, pk) && v != nil {
			if strings.Contains(rawType, "uniqueidentifier") {
				valStr := fmt.Sprintf("%v", v)
				if u, err := uuid.Parse(valStr); err == nil {
					filteredPayload[k] = u.String()
				} else if valStr != "" {
					filteredPayload[k] = uuid.NewSHA1(uuid.NameSpaceDNS, []byte(valStr)).String()
				} else {
					filteredPayload[k] = uuid.NewString()
				}
			} else {
				filteredPayload[k] = v
			}
		}
	}

	if len(filteredPayload) == 0 {
		return true, nil
	}

	var setClauses []string
	var args []interface{}

	for k, v := range filteredPayload {
		setClauses = append(setClauses, fmt.Sprintf("[%s] = ?", k))
		args = append(args, v)
	}
	args = append(args, rowID)

	query := fmt.Sprintf("UPDATE %s SET %s WHERE [%s] = ?", fullTable, strings.Join(setClauses, ", "), pk)
	res, err := mssqlDB.Exec(query, args...)
	if err != nil {
		return false, err
	}

	affected, _ := res.RowsAffected()
	if affected == 0 {
		payload[pk] = rowID
		_, err := insertV2Data(tableName, payload)
		return err == nil, err
	}

	return true, nil
}

// -----------------------------------------------------------------------------
// Batch Endpoint for Ultra-Fast Migration
// -----------------------------------------------------------------------------
func handleV2Batch(w http.ResponseWriter, r *http.Request) {
	if r.Method != "POST" {
		w.WriteHeader(405)
		return
	}

	tableName := strings.TrimPrefix(r.URL.Path, "/api/mysondav2/batch/")
	var items []map[string]interface{}
	if err := json.NewDecoder(r.Body).Decode(&items); err != nil {
		http.Error(w, err.Error(), 400)
		return
	}

	tx, err := mssqlDB.Begin()
	if err != nil {
		http.Error(w, err.Error(), 500)
		return
	}
	defer tx.Rollback()

	inserted := 0
	for _, item := range items {
		_, err := insertV2Data(tableName, item)
		if err == nil {
			inserted++
		}
	}

	if err := tx.Commit(); err != nil {
		http.Error(w, err.Error(), 500)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"success":  true,
		"inserted": inserted,
		"total":    len(items),
	})
}
