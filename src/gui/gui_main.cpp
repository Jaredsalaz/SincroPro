#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include "SyncEngine.hpp"
#include "ODBCConnector.hpp"
#include "APIConnector.hpp"
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <cmath>
#include <algorithm>
#include <memory>
#include <mutex>
#include <thread>

// Define window dimensions
const int WINDOW_WIDTH = 1280;
const int WINDOW_HEIGHT = 720;

// SincroPro namespace wrapper
namespace SincroPro {

// Socket coordinates cache for drawing connection lines
struct SocketPos {
    ImVec2 pos;
    std::string colName;
};

// App State structure
struct AppState {
    SyncEngine syncEngine;
    
    // UI selections
    int selectedSessionIdx = -1;
    std::string selectedTableA = "";
    std::string selectedTableB = "";
    int selectedMappingIdx = -1;

    // Temporary session editor state
    char newSessionName[128] = "";
    char dbAConnStr[512] = "MOCK_DB_A";
    char dbBUrl[512] = "http://localhost:5000/api";
    int intervalSeconds = 10;

    // Table/Column mapping temporary select
    std::string activeSourceCol = "";
    std::string activeTargetCol = "";
    
    // DB schemas cache for visual mapper
    std::vector<std::string> tablesA;
    std::vector<std::string> tablesB;
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> colsA;
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> colsB;

    // Search filters
    char filterTableA[128] = "";
    char filterTableB[128] = "";

    std::vector<SocketPos> leftSockets;
    std::vector<SocketPos> rightSockets;

    bool dbAConnected = false;
    bool dbBConnected = false;

    // Async schema loading state
    std::mutex dbSchemasMutex;
    bool dbALoading = false;
    bool dbBLoading = false;
    std::string connectionErrorMsg = "";
    bool triggerErrorPopup = false;
    bool migratingData = false;
};

// Premium Glassmorphic Styling
void ApplyPremiumDarkTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Roundings
    style.WindowRounding = 10.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 12.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    // Sleek glassmorphism color palette
    colors[ImGuiCol_Text]                   = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.58f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.08f, 0.08f, 0.12f, 0.96f); // Glass window back
    colors[ImGuiCol_ChildBg]                = ImVec4(0.11f, 0.11f, 0.16f, 0.50f); // Semi-transparent panels
    colors[ImGuiCol_PopupBg]                = ImVec4(0.09f, 0.09f, 0.14f, 0.98f);
    colors[ImGuiCol_Border]                 = ImVec4(1.00f, 1.00f, 1.00f, 0.08f); // Soft glass border
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.14f, 0.14f, 0.20f, 0.60f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.20f, 0.20f, 0.28f, 0.70f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.24f, 0.24f, 0.35f, 0.80f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.07f, 0.07f, 0.10f, 0.98f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.11f, 0.11f, 0.16f, 0.98f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.07f, 0.07f, 0.10f, 0.98f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.09f, 0.09f, 0.13f, 0.98f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.05f, 0.05f, 0.08f, 0.30f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.20f, 0.20f, 0.28f, 0.80f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.30f, 0.30f, 0.40f, 0.80f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.40f, 0.40f, 0.50f, 0.80f);
    
    // Vibrant Neon accents (Electric Cyan/Purple)
    colors[ImGuiCol_CheckMark]              = ImVec4(0.00f, 0.85f, 1.00f, 1.00f); // Cyan
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.60f, 0.20f, 1.00f, 0.80f); // Purple
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.60f, 0.20f, 1.00f, 1.00f);
    colors[ImGuiCol_Button]                 = ImVec4(0.50f, 0.15f, 0.90f, 0.50f); // Glass purple
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.60f, 0.20f, 1.00f, 0.80f); // Vibrant purple
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.00f, 0.85f, 1.00f, 0.80f); // Cyan punch
    colors[ImGuiCol_Header]                 = ImVec4(0.25f, 0.15f, 0.45f, 0.45f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.35f, 0.20f, 0.65f, 0.60f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.00f, 0.85f, 1.00f, 0.60f);
    colors[ImGuiCol_Separator]              = ImVec4(1.00f, 1.00f, 1.00f, 0.08f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.00f, 0.85f, 1.00f, 0.60f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.00f, 0.85f, 1.00f, 1.00f);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.20f, 0.20f, 0.28f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.30f, 0.30f, 0.40f, 0.60f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.60f, 0.20f, 1.00f, 0.80f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.12f, 0.12f, 0.18f, 0.80f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.25f, 0.15f, 0.45f, 0.80f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.35f, 0.20f, 0.65f, 0.90f);
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.08f, 0.08f, 0.12f, 0.80f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.12f, 0.12f, 0.18f, 0.90f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.00f, 0.85f, 1.00f, 0.25f);
    colors[ImGuiCol_DragDropTarget]         = ImVec4(0.00f, 0.85f, 1.00f, 0.90f);
}

// Draw a premium loading spinner using drawing primitives
void DrawSpinner(const char* id, float radius, float thickness, ImU32 color) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(radius * 2.0f, radius * 2.0f);
    
    ImGui::Dummy(size);
    
    ImVec2 center = ImVec2(pos.x + radius, pos.y + radius);
    float time = (float)ImGui::GetTime();
    
    int numSegments = 30;
    float startAngle = time * 6.0f;
    float endAngle = startAngle + (3.14159265f * 1.5f); // 270 degree arc
    
    drawList->PathClear();
    for (int i = 0; i <= numSegments; i++) {
        float angle = startAngle + (float)i / (float)numSegments * (endAngle - startAngle);
        drawList->PathLineTo(ImVec2(center.x + cosf(angle) * radius, center.y + sinf(angle) * radius));
    }
    drawList->PathStroke(color, false, thickness);
}

// Function to refresh schemas for selected session asynchronously
void RefreshDatabaseSchemas(AppState& state) {
    if (state.selectedSessionIdx < 0 || state.selectedSessionIdx >= (int)state.syncEngine.getSessions().size()) {
        return;
    }

    state.dbALoading = true;
    state.dbBLoading = true;
    
    // Clear previous error messages (already under GUI lock when clicked, or safe at startup)
    state.connectionErrorMsg = "";

    // Capture session copy to safely reference in threads
    const auto session = state.syncEngine.getSessions()[state.selectedSessionIdx];
    
    // Thread A: Fetch BD A (MySQL)
    std::thread([&state, session]() {
        bool connectedA = false;
        std::vector<std::string> tempTablesA;
        std::map<std::string, std::vector<std::pair<std::string, std::string>>> tempColsA;
        std::string errorMsgA = "";

        std::unique_ptr<DatabaseConnector> dbA;
        if (session.dbA_connStr.rfind("http://", 0) == 0 || session.dbA_connStr.rfind("https://", 0) == 0) {
            dbA = std::make_unique<APIConnector>();
        } else {
            dbA = std::make_unique<ODBCConnector>();
        }

        connectedA = dbA->connect(session.dbA_connStr);
        if (connectedA) {
            tempTablesA = dbA->getTables();
            if (tempTablesA.empty()) {
                connectedA = false;
                errorMsgA = "BD A (" + session.dbA_connStr + "):\nConectado a la API pero no se cargaron tablas. Verifica que la base de datos MySQL local este activa en el puerto 3307.\n\n";
            } else {
                for (const auto& t : tempTablesA) {
                    tempColsA[t] = dbA->getColumns(t);
                }
            }
            dbA->disconnect();
        } else {
            errorMsgA = "BD A (" + session.dbA_connStr + "):\nError al conectar al endpoint API. Asegurate de que el script 'mysondav1_api.py' este corriendo en el puerto 5001.\n\n";
        }

        // Lock and update AppState schemas for BD A
        {
            std::lock_guard<std::mutex> lock(state.dbSchemasMutex);
            state.dbAConnected = connectedA;
            state.tablesA = std::move(tempTablesA);
            state.colsA = std::move(tempColsA);
            state.leftSockets.clear();

            if (!connectedA) {
                state.connectionErrorMsg += errorMsgA;
                state.triggerErrorPopup = true;
            }
            state.dbALoading = false;
        }
    }).detach();

    // Thread B: Fetch BD B (SQL Server)
    std::thread([&state, session]() {
        bool connectedB = false;
        std::vector<std::string> tempTablesB;
        std::map<std::string, std::vector<std::pair<std::string, std::string>>> tempColsB;
        std::string errorMsgB = "";

        std::unique_ptr<DatabaseConnector> dbB;
        if (session.dbB_url.rfind("http://", 0) == 0 || session.dbB_url.rfind("https://", 0) == 0) {
            dbB = std::make_unique<APIConnector>();
        } else {
            dbB = std::make_unique<ODBCConnector>();
        }

        connectedB = dbB->connect(session.dbB_url);
        if (connectedB) {
            tempTablesB = dbB->getTables();
            if (tempTablesB.empty()) {
                connectedB = false;
                errorMsgB = "BD B (" + session.dbB_url + "):\nConectado a la API pero no se cargaron tablas. Verifica que las credenciales de SQL Server sean correctas.\n\n";
            } else {
                for (const auto& t : tempTablesB) {
                    tempColsB[t] = dbB->getColumns(t);
                }
            }
            dbB->disconnect();
        } else {
            errorMsgB = "BD B (" + session.dbB_url + "):\nError al conectar al endpoint API. Asegurate de que 'mysondav2_api.py' este corriendo en el puerto 5002 y la VPN este activa.\n\n";
        }

        // Lock and update AppState schemas for BD B
        {
            std::lock_guard<std::mutex> lock(state.dbSchemasMutex);
            state.dbBConnected = connectedB;
            state.tablesB = std::move(tempTablesB);
            state.colsB = std::move(tempColsB);
            state.rightSockets.clear();

            if (!connectedB) {
                state.connectionErrorMsg += errorMsgB;
                state.triggerErrorPopup = true;
            }
            state.dbBLoading = false;
        }
    }).detach();
}

// Draw a beautiful custom Bézier curve mapping connection
void DrawMappingConnection(ImVec2 start, ImVec2 end, bool selected) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    
    // Control points for smooth horizontal S-curve
    float dx = std::abs(end.x - start.x) * 0.5f;
    ImVec2 cp1 = ImVec2(start.x + dx, start.y);
    ImVec2 cp2 = ImVec2(end.x - dx, end.y);
    
    ImU32 color = selected ? ImGui::GetColorU32(ImVec4(0.00f, 0.85f, 1.00f, 1.00f)) // Neon Cyan
                           : ImGui::GetColorU32(ImVec4(0.60f, 0.20f, 1.00f, 0.60f)); // Electric Purple
    
    float thickness = selected ? 3.0f : 1.8f;
    
    // Shadow / glow path
    drawList->AddBezierCubic(start, cp1, cp2, end, ImGui::GetColorU32(ImVec4(0.60f, 0.20f, 1.00f, 0.15f)), thickness + 4.0f);
    // Core line
    drawList->AddBezierCubic(start, cp1, cp2, end, color, thickness);
    
    // Draw circles at start and end
    drawList->AddCircleFilled(start, 4.0f, color);
    drawList->AddCircleFilled(end, 4.0f, color);
}

// GUI Drawing Loop
void RenderUI(AppState& state) {
    // Thread safety lock for database schemas
    std::lock_guard<std::mutex> lock(state.dbSchemasMutex);

    float windowWidth = ImGui::GetIO().DisplaySize.x;
    float windowHeight = ImGui::GetIO().DisplaySize.y;

    // 1. Sidebar Session Manager
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(300, windowHeight));
    ImGui::Begin("SessionsSidebar", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
    
    ImGui::TextColored(ImVec4(0.00f, 0.85f, 1.00f, 1.00f), "SINCRO PRO v1.0");
    ImGui::Separator();
    ImGui::Spacing();

    // Session creation controls
    ImGui::Text("Crear Nueva Sesion:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##newSess", "Nombre de sesion...", state.newSessionName, IM_ARRAYSIZE(state.newSessionName));
    
    if (ImGui::Button("Agregar Sesion", ImVec2(-1, 0))) {
        if (strlen(state.newSessionName) > 0) {
            SyncSessionConfig s;
            s.name = state.newSessionName;
            s.dbA_connStr = "MOCK_DB_A";
            s.dbB_url = "http://localhost:5000/api";
            s.intervalSeconds = 30;
            s.active = false;
            
            state.syncEngine.addSession(s);
            state.newSessionName[0] = '\0'; // clear input
            state.selectedSessionIdx = (int)state.syncEngine.getSessions().size() - 1;
            
            // Populate config editor text fields
            strcpy(state.dbAConnStr, s.dbA_connStr.c_str());
            strcpy(state.dbBUrl, s.dbB_url.c_str());
            state.intervalSeconds = s.intervalSeconds;

            RefreshDatabaseSchemas(state);
        }
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    ImGui::Text("Sesiones de Mapeo:");
    
    auto& sessions = state.syncEngine.getSessions();
    for (int i = 0; i < (int)sessions.size(); ++i) {
        bool isSelected = (state.selectedSessionIdx == i);
        
        std::string label = sessions[i].name + (sessions[i].active ? " (RUNNING)" : " (IDLE)");
        if (ImGui::Selectable(label.c_str(), isSelected)) {
            state.selectedSessionIdx = i;
            
            // Populate fields
            strcpy(state.dbAConnStr, sessions[i].dbA_connStr.c_str());
            strcpy(state.dbBUrl, sessions[i].dbB_url.c_str());
            state.intervalSeconds = sessions[i].intervalSeconds;
            
            state.selectedTableA = "";
            state.selectedTableB = "";
            state.selectedMappingIdx = -1;

            RefreshDatabaseSchemas(state);
        }

        // Context menu for each session
        if (ImGui::BeginPopupContextItem()) {
            if (sessions[i].active) {
                if (ImGui::MenuItem("Detener Servicio")) {
                    state.syncEngine.stopSession(sessions[i].name);
                }
            } else {
                if (ImGui::MenuItem("Arrancar Servicio")) {
                    state.syncEngine.startSession(sessions[i].name);
                }
            }

            if (ImGui::MenuItem("Ejecutar Sincronizacion")) {
                state.syncEngine.runSyncOnce(sessions[i].name);
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Eliminar Sesion")) {
                state.syncEngine.deleteSession(sessions[i].name);
                state.selectedSessionIdx = -1;
            }
            ImGui::EndPopup();
        }
    }

    ImGui::End();

    // Work Area
    ImGui::SetNextWindowPos(ImVec2(300, 0));
    ImGui::SetNextWindowSize(ImVec2(windowWidth - 300, windowHeight));
    ImGui::Begin("WorkSpace", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);

    if (state.selectedSessionIdx < 0 || state.selectedSessionIdx >= (int)sessions.size()) {
        // Welcome View
        ImGui::Text("Selecciona o crea una sesion de mapeo en la barra lateral para comenzar.");
        ImGui::End();
        return;
    }

    auto& session = sessions[state.selectedSessionIdx];

    // Session controls / Connection info header
    ImGui::Text("Configuracion de Sesion: "); ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.00f, 0.85f, 1.00f, 1.00f), "%s", session.name.c_str());
    
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.22f, 0.30f));
    ImGui::BeginChild("ConnConfig", ImVec2(-1, 95), true);
    
    ImGui::Columns(3, nullptr, false);
    
    ImGui::Text("BD A (Local ODBC)");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##dbAConn", state.dbAConnStr, IM_ARRAYSIZE(state.dbAConnStr))) {
        session.dbA_connStr = state.dbAConnStr;
    }
    
    ImGui::NextColumn();
    ImGui::Text("BD B (Endpoint API)");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##dbBConn", state.dbBUrl, IM_ARRAYSIZE(state.dbBUrl))) {
        session.dbB_url = state.dbBUrl;
    }

    ImGui::NextColumn();
    ImGui::Text("Programacion (Segundos)");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputInt("##interval", &state.intervalSeconds)) {
        if (state.intervalSeconds < 1) state.intervalSeconds = 1;
        session.intervalSeconds = state.intervalSeconds;
    }
    
    ImGui::Columns(1);
    
    if (ImGui::Button("Probar y Conectar Bases de Datos", ImVec2(-1, 26))) {
        state.syncEngine.updateSession(session.name, session);
        RefreshDatabaseSchemas(state);
    }
    
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // Start workspace views tab bar
    if (ImGui::BeginTabBar("WorkspaceTabBar")) {
        
        // TAB 1: VISUAL SCHEMA MAPPER
        if (ImGui::BeginTabItem("Configuración de Mapeo")) {
            float colWidth = (windowWidth - 300.0f - 40.0f) / 3.0f;
            float panelsHeight = std::max(150.0f, windowHeight - 390.0f);
            
            // Panel 1: Left Database A Browser
            ImGui::BeginChild("PanelLeft", ImVec2(colWidth, panelsHeight), true);
            ImGui::TextColored(ImVec4(0.60f, 0.20f, 1.00f, 1.00f), "BD A (ORIGEN)");
            
            if (state.dbALoading) {
                ImGui::Text("Estado: Cargando...");
                ImGui::Spacing();
                DrawSpinner("spinnerA", 15.0f, 3.0f, ImGui::GetColorU32(ImVec4(0.60f, 0.20f, 1.00f, 1.00f)));
                ImGui::TextWrapped("Cargando esquema de la BD A...");
            } else {
                ImGui::Text(state.dbAConnected ? "Estado: Conectado" : "Estado: Desconectado");
                
                ImGui::InputTextWithHint("##filterA", "Buscar tabla...", state.filterTableA, IM_ARRAYSIZE(state.filterTableA));
                ImGui::Separator();
                
                std::string searchFilterA = state.filterTableA;
                std::transform(searchFilterA.begin(), searchFilterA.end(), searchFilterA.begin(), ::tolower);

                for (const auto& t : state.tablesA) {
                    std::string tableLower = t;
                    std::transform(tableLower.begin(), tableLower.end(), tableLower.begin(), ::tolower);
                    if (!searchFilterA.empty() && tableLower.find(searchFilterA) == std::string::npos) {
                        continue;
                    }

                    bool isSelected = (state.selectedTableA == t);
                    if (ImGui::Selectable(t.c_str(), isSelected)) {
                        state.selectedTableA = t;
                        state.activeSourceCol = ""; // Reset selected col mapping
                    }
                }
            }
            ImGui::EndChild();

            ImGui::SameLine();

            // Panel 2: Center Mapping Config / Connection Lines Workspace
            ImGui::BeginChild("PanelCenter", ImVec2(colWidth, panelsHeight), true);
            ImGui::TextColored(ImVec4(0.00f, 0.85f, 1.00f, 1.00f), "MAPEO Y RELACIONES");
            
            if (state.selectedTableA.empty() || state.selectedTableB.empty()) {
                ImGui::TextWrapped("Selecciona una tabla origen (izquierda) y destino (derecha) para crear mapeo.");
            } else {
                ImGui::Text("Origen: %s", state.selectedTableA.c_str());
                ImGui::Text("Destino: %s", state.selectedTableB.c_str());
                
                // Find existing mapping or prepare details to create one
                int mappingIdx = -1;
                for (int i = 0; i < (int)session.mappings.size(); ++i) {
                    if (session.mappings[i].tableA == state.selectedTableA && session.mappings[i].tableB == state.selectedTableB) {
                        mappingIdx = i;
                        break;
                    }
                }

                if (mappingIdx == -1) {
                    if (ImGui::Button("Registrar Relacion de Tabla", ImVec2(-1, 0))) {
                        TableMapping newMap;
                        newMap.tableA = state.selectedTableA;
                        newMap.tableB = state.selectedTableB;
                        newMap.direction = "A_TO_B";
                        newMap.idGenerator = false;
                        
                        session.mappings.push_back(newMap);
                        state.syncEngine.updateSession(session.name, session);
                        mappingIdx = (int)session.mappings.size() - 1;
                    }
                }

                if (mappingIdx != -1) {
                    auto& tm = session.mappings[mappingIdx];
                    
                    ImGui::Checkbox("Esta Tabla Genera IDs/GUIDs", &tm.idGenerator);
                    ImGui::SameLine();
                    if (ImGui::Button("Eliminar Mapeo", ImVec2(-1, 0))) {
                        session.mappings.erase(session.mappings.begin() + mappingIdx);
                        state.syncEngine.updateSession(session.name, session);
                        mappingIdx = -1;
                    }

                    if (mappingIdx != -1) {
                        ImGui::Separator();
                        ImGui::Text("Conexiones de Columnas:");
                        
                        // Clear sockets cache for drawing lines
                        state.leftSockets.clear();
                        state.rightSockets.clear();

                        // Left columns list
                        ImGui::Columns(2, "colsMappers", false);
                        ImGui::Text("Columna BD A");
                        ImGui::Separator();

                        auto itColsA = state.colsA.find(tm.tableA);
                        if (itColsA != state.colsA.end()) {
                            for (const auto& col : itColsA->second) {
                                bool isSelected = (state.activeSourceCol == col.first);
                                ImGui::PushID(("src_" + col.first).c_str());
                                
                                std::string label = col.first + " (" + col.second + ")";
                                if (ImGui::Selectable(label.c_str(), isSelected)) {
                                    state.activeSourceCol = col.first;
                                }
                                
                                // Register socket position
                                ImVec2 textPos = ImGui::GetCursorScreenPos();
                                SocketPos sp;
                                sp.pos = ImVec2(textPos.x + ImGui::GetColumnWidth() - 10, textPos.y - ImGui::GetTextLineHeight() * 0.5f);
                                sp.colName = col.first;
                                state.leftSockets.push_back(sp);

                                ImGui::PopID();
                            }
                        }

                        // Right columns list
                        ImGui::NextColumn();
                        ImGui::Text("Columna BD B");
                        ImGui::Separator();

                        auto itColsB = state.colsB.find(tm.tableB);
                        if (itColsB != state.colsB.end()) {
                            for (const auto& col : itColsB->second) {
                                bool isSelected = (state.activeTargetCol == col.first);
                                ImGui::PushID(("dst_" + col.first).c_str());
                                
                                std::string label = col.first + " (" + col.second + ")";
                                if (ImGui::Selectable(label.c_str(), isSelected)) {
                                    state.activeTargetCol = col.first;
                                }

                                // Register socket position
                                ImVec2 textPos = ImGui::GetCursorScreenPos();
                                SocketPos sp;
                                sp.pos = ImVec2(textPos.x + 10, textPos.y - ImGui::GetTextLineHeight() * 0.5f);
                                sp.colName = col.first;
                                state.rightSockets.push_back(sp);

                                ImGui::PopID();
                            }
                        }
                        ImGui::Columns(1);

                        // Relate button
                        ImGui::Separator();
                        if (!state.activeSourceCol.empty() && !state.activeTargetCol.empty()) {
                            if (ImGui::Button("Establecer Relacion (Conectar)", ImVec2(-1, 0))) {
                                // Check if column already mapped
                                bool found = false;
                                for (auto& colMap : tm.columns) {
                                    if (colMap.colA == state.activeSourceCol) {
                                        colMap.colB = state.activeTargetCol;
                                        found = true;
                                        break;
                                    }
                                }

                                if (!found) {
                                    ColumnMapping cm;
                                    cm.colA = state.activeSourceCol;
                                    cm.colB = state.activeTargetCol;
                                    cm.isKey = (state.activeSourceCol == "id" || state.activeSourceCol == "id_cliente" || state.activeSourceCol == "id_item" || state.activeSourceCol == "id_trx");
                                    cm.translateViaTable = "";
                                    tm.columns.push_back(cm);
                                }

                                state.syncEngine.updateSession(session.name, session);
                                state.activeSourceCol = "";
                                state.activeTargetCol = "";
                            }
                        } else {
                            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Selecciona una columna izquierda y derecha para conectar");
                        }

                        // Render current mappings settings list below
                        ImGui::Separator();
                        ImGui::Text("Mapeos Activos:");
                        for (size_t k = 0; k < tm.columns.size(); ++k) {
                            auto& colMap = tm.columns[k];
                            ImGui::PushID(static_cast<int>(k));
                            
                            ImGui::Text("%s -> %s", colMap.colA.c_str(), colMap.colB.c_str());
                            
                            ImGui::SameLine(ImGui::GetWindowWidth() - 200);
                            ImGui::Checkbox("Llave", &colMap.isKey);
                            
                            ImGui::SameLine(ImGui::GetWindowWidth() - 100);
                            if (ImGui::Button("Borrar")) {
                                tm.columns.erase(tm.columns.begin() + k);
                                state.syncEngine.updateSession(session.name, session);
                                ImGui::PopID();
                                break;
                            }

                            // Setup ID translation dictionary reference
                            char transTable[128] = "";
                            strcpy(transTable, colMap.translateViaTable.c_str());
                            ImGui::SetNextItemWidth(150);
                            if (ImGui::InputText("Traductor ID", transTable, IM_ARRAYSIZE(transTable))) {
                                colMap.translateViaTable = transTable;
                                state.syncEngine.updateSession(session.name, session);
                            }
                            ImGui::PopID();
                        }
                    }
                }
            }
            ImGui::EndChild();

            ImGui::SameLine();

            // Panel 3: Right Database B Browser
            ImGui::BeginChild("PanelRight", ImVec2(colWidth, panelsHeight), true);
            ImGui::TextColored(ImVec4(0.00f, 0.85f, 1.00f, 1.00f), "BD B (DESTINO)");
            
            if (state.dbBLoading) {
                ImGui::Text("Estado: Cargando...");
                ImGui::Spacing();
                DrawSpinner("spinnerB", 15.0f, 3.0f, ImGui::GetColorU32(ImVec4(0.00f, 0.85f, 1.00f, 1.00f)));
                ImGui::TextWrapped("Cargando esquema de la BD B...");
            } else {
                ImGui::Text(state.dbBConnected ? "Estado: Conectado" : "Estado: Desconectado");
                
                ImGui::InputTextWithHint("##filterB", "Buscar tabla...", state.filterTableB, IM_ARRAYSIZE(state.filterTableB));
                ImGui::Separator();
                
                std::string searchFilterB = state.filterTableB;
                std::transform(searchFilterB.begin(), searchFilterB.end(), searchFilterB.begin(), ::tolower);

                for (const auto& t : state.tablesB) {
                    std::string tableLower = t;
                    std::transform(tableLower.begin(), tableLower.end(), tableLower.begin(), ::tolower);
                    if (!searchFilterB.empty() && tableLower.find(searchFilterB) == std::string::npos) {
                        continue;
                    }

                    bool isSelected = (state.selectedTableB == t);
                    if (ImGui::Selectable(t.c_str(), isSelected)) {
                        state.selectedTableB = t;
                        state.activeTargetCol = ""; // Reset selected col mapping
                    }
                }
            }
            ImGui::EndChild();

            // Dynamic Bézier connection lines rendering in background of columns
            if (!state.selectedTableA.empty() && !state.selectedTableB.empty()) {
                int mappingIdx = -1;
                for (int i = 0; i < (int)session.mappings.size(); ++i) {
                    if (session.mappings[i].tableA == state.selectedTableA && session.mappings[i].tableB == state.selectedTableB) {
                        mappingIdx = i;
                        break;
                    }
                }

                if (mappingIdx != -1) {
                    const auto& tm = session.mappings[mappingIdx];
                    for (const auto& colMap : tm.columns) {
                        ImVec2 start(0, 0), end(0, 0);
                        bool startFound = false, endFound = false;

                        for (const auto& socket : state.leftSockets) {
                            if (socket.colName == colMap.colA) {
                                start = socket.pos;
                                startFound = true;
                                break;
                            }
                        }
                        for (const auto& socket : state.rightSockets) {
                            if (socket.colName == colMap.colB) {
                                end = socket.pos;
                                endFound = true;
                                break;
                            }
                        }

                        if (startFound && endFound) {
                            bool selected = (state.activeSourceCol == colMap.colA || state.activeTargetCol == colMap.colB);
                            DrawMappingConnection(start, end, selected);
                        }
                    }
                }
            }
            ImGui::EndTabItem();
        }

        // TAB 2: REPLIX MONITOR & MIGRATION CONSOLE
        if (ImGui::BeginTabItem("Consola y Réplica de Datos")) {
            float panelsHeight = std::max(150.0f, windowHeight - 390.0f);
            
            ImGui::BeginChild("ReplixPanelArea", ImVec2(-1, panelsHeight), true);
            
            // Graphic Schema Relations Map
            ImGui::TextColored(ImVec4(0.00f, 0.85f, 1.00f, 1.00f), "DIAGRAMA DE FLUJO DE RELACIONES:");
            
            ImGui::BeginChild("RelationsCanvas", ImVec2(-1, 160), true, ImGuiWindowFlags_HorizontalScrollbar);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 cursor = ImGui::GetCursorScreenPos();
            
            float cardWidth = 190.0f;
            float cardHeight = 36.0f;
            float gapY = 46.0f;

            if (session.mappings.empty()) {
                ImGui::SetCursorPos(ImVec2(15.0f, 60.0f));
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No hay tablas mapeadas en esta sesion. Configura relaciones en la primera pestaña.");
            } else {
                float time = (float)ImGui::GetTime();
                for (size_t i = 0; i < session.mappings.size(); ++i) {
                    const auto& tm = session.mappings[i];
                    
                    ImVec2 posA(cursor.x + 30.0f, cursor.y + 10.0f + i * gapY);
                    ImVec2 posB(cursor.x + 340.0f, cursor.y + 10.0f + i * gapY);
                    
                    // Hover detection
                    ImVec2 cardA_min = posA;
                    ImVec2 cardA_max = ImVec2(posA.x + cardWidth, posA.y + cardHeight);
                    ImVec2 cardB_min = posB;
                    ImVec2 cardB_max = ImVec2(posB.x + cardWidth, posB.y + cardHeight);
                    
                    bool hoveredA = ImGui::IsMouseHoveringRect(cardA_min, cardA_max);
                    bool hoveredB = ImGui::IsMouseHoveringRect(cardB_min, cardB_max);
                    
                    if ((hoveredA || hoveredB) && ImGui::IsMouseClicked(0)) {
                        state.selectedMappingIdx = (int)i;
                    }
                    
                    bool isSelected = (state.selectedMappingIdx == (int)i);
                    
                    // Background Colors
                    ImU32 colBgA = isSelected ? ImGui::GetColorU32(ImVec4(0.24f, 0.16f, 0.36f, 0.95f)) : ImGui::GetColorU32(ImVec4(0.18f, 0.12f, 0.28f, 0.85f));
                    ImU32 colBgB = isSelected ? ImGui::GetColorU32(ImVec4(0.10f, 0.20f, 0.30f, 0.95f)) : ImGui::GetColorU32(ImVec4(0.10f, 0.18f, 0.25f, 0.85f));
                    
                    // Border Colors with Glow and effects
                    ImU32 colBorderA;
                    ImU32 colBorderB;
                    if (isSelected) {
                        float pulse = (sinf(time * 6.0f) + 1.0f) * 0.5f;
                        colBorderA = ImGui::GetColorU32(ImVec4(0.70f + pulse * 0.30f, 0.20f, 1.00f, 1.00f));
                        colBorderB = ImGui::GetColorU32(ImVec4(0.00f, 0.85f + pulse * 0.15f, 1.00f, 1.00f));
                    } else if (hoveredA || hoveredB) {
                        colBorderA = ImGui::GetColorU32(ImVec4(0.80f, 0.40f, 1.00f, 0.90f));
                        colBorderB = ImGui::GetColorU32(ImVec4(0.00f, 0.95f, 1.00f, 0.90f));
                    } else {
                        colBorderA = ImGui::GetColorU32(ImVec4(0.60f, 0.20f, 1.00f, 0.50f));
                        colBorderB = ImGui::GetColorU32(ImVec4(0.00f, 0.85f, 1.00f, 0.50f));
                    }
                    
                    // Draw Card A (Source MySQL Table)
                    drawList->AddRectFilled(posA, ImVec2(posA.x + cardWidth, posA.y + cardHeight), colBgA, 5.0f);
                    drawList->AddRect(posA, ImVec2(posA.x + cardWidth, posA.y + cardHeight), colBorderA, 5.0f, 0, isSelected ? 2.2f : 1.5f);
                    std::string labelA = "MySQL: " + tm.tableA;
                    drawList->AddText(ImVec2(posA.x + 12.0f, posA.y + 10.0f), ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.95f, 1.00f)), labelA.c_str());
                    
                    // Draw Card B (Target SQL Server Table)
                    drawList->AddRectFilled(posB, ImVec2(posB.x + cardWidth, posB.y + cardHeight), colBgB, 5.0f);
                    drawList->AddRect(posB, ImVec2(posB.x + cardWidth, posB.y + cardHeight), colBorderB, 5.0f, 0, isSelected ? 2.2f : 1.5f);
                    std::string labelB = "SQLServer: " + tm.tableB;
                    drawList->AddText(ImVec2(posB.x + 12.0f, posB.y + 10.0f), ImGui::GetColorU32(ImVec4(0.9f, 0.95f, 0.95f, 1.00f)), labelB.c_str());
                    
                    // Draw Connecting horizontal line with dynamic arrows and pulses
                    ImVec2 pinA(posA.x + cardWidth, posA.y + cardHeight * 0.5f);
                    ImVec2 pinB(posB.x, posB.y + cardHeight * 0.5f);
                    
                    ImU32 colLine;
                    float thickness = 2.0f;
                    if (isSelected) {
                        float pulse = (sinf(time * 8.0f) + 1.0f) * 0.5f;
                        colLine = ImGui::GetColorU32(ImVec4(0.00f, 0.85f, 1.00f, 0.80f + pulse * 0.20f));
                        thickness = 2.0f + pulse * 2.0f;
                        
                        // Draw glow shadow
                        drawList->AddLine(pinA, pinB, ImGui::GetColorU32(ImVec4(0.00f, 0.85f, 1.00f, 0.15f)), thickness + 6.0f);
                    } else if (hoveredA || hoveredB) {
                        colLine = ImGui::GetColorU32(ImVec4(0.00f, 0.95f, 1.00f, 0.90f));
                        thickness = 2.5f;
                    } else {
                        colLine = ImGui::GetColorU32(ImVec4(0.00f, 0.85f, 1.00f, 0.60f));
                    }
                    
                    drawList->AddLine(pinA, pinB, colLine, thickness);
                    drawList->AddTriangleFilled(ImVec2(pinB.x, pinB.y), ImVec2(pinB.x - 8.0f, pinB.y - 5.0f), ImVec2(pinB.x - 8.0f, pinB.y + 5.0f), colLine);
                }
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (session.mappings.size() * gapY) + 15.0f);
            }
            ImGui::EndChild();

            // Selected relation details box below schema map
            if (state.selectedMappingIdx >= 0 && state.selectedMappingIdx < (int)session.mappings.size()) {
                const auto& tm = session.mappings[state.selectedMappingIdx];
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.00f, 0.85f, 1.00f, 1.00f), "Detalles de Relación Seleccionada: %s -> %s", tm.tableA.c_str(), tm.tableB.c_str());
                
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.10f, 0.18f, 0.40f));
                ImGui::BeginChild("RelationDetails", ImVec2(-1, 80), true);
                
                if (tm.columns.empty()) {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No hay columnas conectadas para esta tabla en la pestaña de Mapeo.");
                } else {
                    ImGui::Columns(2, "detailColumns", false);
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Columna BD Origen (MySQL)");
                    ImGui::NextColumn();
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Columna BD Destino (SQL Server)");
                    ImGui::NextColumn();
                    ImGui::Separator();
                    
                    for (const auto& col : tm.columns) {
                        ImGui::TextColored(ImVec4(0.60f, 0.20f, 1.00f, 1.00f), "  [BD A] %s", col.colA.c_str());
                        ImGui::NextColumn();
                        ImGui::TextColored(ImVec4(0.00f, 0.85f, 1.00f, 1.00f), "  ===>  [BD B] %s %s", col.colB.c_str(), col.isKey ? "(LLAVE)" : "");
                        ImGui::NextColumn();
                    }
                    ImGui::Columns(1);
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
            } else {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "* Haz clic en cualquier tarjeta del diagrama de relaciones de arriba para ver el mapeo detallado de sus columnas.");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // REPLIX & Migration Controls Columns
            ImGui::Columns(2, "ReplixCols", false);
            
            // COLUMN 1: REPLIX CONTINOUS SYNC
            ImGui::Text("Servicio de Replica Continua (Segundos: %d):", session.intervalSeconds);
            ImGui::Spacing();
            
            bool isReplixRunning = session.active;
            if (isReplixRunning) {
                // Active status button
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.65f, 0.25f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.75f, 0.30f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.00f, 0.55f, 0.20f, 1.00f));
                if (ImGui::Button("REPLIX: ACTIVO", ImVec2(220, 45))) {
                    state.syncEngine.stopSession(session.name);
                }
                ImGui::PopStyleColor(3);
                ImGui::SameLine();
                
                ImGui::BeginGroup();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
                DrawSpinner("replixSpinner", 10.0f, 2.5f, ImGui::GetColorU32(ImVec4(0.00f, 0.85f, 0.35f, 1.00f)));
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.00f, 0.85f, 0.35f, 1.00f), "Sincronizando...");
                ImGui::EndGroup();
            } else {
                // Inactive status button
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.20f, 0.25f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.30f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.15f, 0.20f, 1.00f));
                if (ImGui::Button("ACTIVAR REPLIX", ImVec2(220, 45))) {
                    state.syncEngine.startSession(session.name);
                }
                ImGui::PopStyleColor(3);
                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12.0f);
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Servicio en Pausa");
            }

            ImGui::NextColumn();

            // COLUMN 2: MANUAL DATA MIGRATION
            ImGui::Text("Migracion Manual Unica:");
            ImGui::Spacing();
            
            if (state.migratingData) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
                ImGui::Button("MIGRANDO DATOS...", ImVec2(220, 45));
                ImGui::PopStyleColor();
                ImGui::SameLine();
                
                ImGui::BeginGroup();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
                DrawSpinner("migratingSpinner", 10.0f, 2.5f, ImGui::GetColorU32(ImVec4(0.00f, 0.85f, 1.00f, 1.00f)));
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.00f, 0.85f, 1.00f, 1.00f), "Migrando...");
                ImGui::EndGroup();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.20f, 1.00f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.30f, 1.00f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.50f, 0.15f, 0.90f, 1.00f));
                if (ImGui::Button("MIGRAR DATOS AHORA", ImVec2(220, 45))) {
                    state.migratingData = true;
                    // Run manual migration process in background thread
                    std::thread([&state, session]() mutable {
                        state.syncEngine.executeSyncProcess(session);
                        state.migratingData = false;
                    }).detach();
                }
                ImGui::PopStyleColor(3);
            }
            ImGui::Columns(1);
            
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Spacing();

    // Logging Console / Monitor at the bottom
    ImGui::Text("Terminal de Consola de Sincronizacion (Logs en Tiempo Real):");
    
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.05f, 0.08f, 0.85f));
    ImGui::BeginChild("ConsoleLog", ImVec2(-1, -1), true, ImGuiWindowFlags_HorizontalScrollbar);
    
    auto logs = state.syncEngine.getLogs();
    for (const auto& logMsg : logs) {
        // Simple color coding: Error is red, Warning is orange, normal is white/gray
        if (logMsg.find("Error") != std::string::npos) {
            ImGui::TextColored(ImVec4(1.00f, 0.25f, 0.25f, 1.00f), "%s", logMsg.c_str());
        } else if (logMsg.find("Warning") != std::string::npos) {
            ImGui::TextColored(ImVec4(1.00f, 0.60f, 0.00f, 1.00f), "%s", logMsg.c_str());
        } else if (logMsg.find("completed") != std::string::npos || logMsg.find("Started") != std::string::npos) {
            ImGui::TextColored(ImVec4(0.00f, 0.85f, 1.00f, 1.00f), "%s", logMsg.c_str());
        } else {
            ImGui::TextUnformatted(logMsg.c_str());
        }
    }
    
    // Auto-scroll to bottom on new log messages
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    // Trigger error popup from main thread
    if (state.triggerErrorPopup) {
        ImGui::OpenPopup("Error de Conexion");
        state.triggerErrorPopup = false;
    }

    // Modal popup rendering
    if (ImGui::BeginPopupModal("Error de Conexion", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(1.00f, 0.25f, 0.25f, 1.00f), "ERROR DE CONEXION");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextWrapped("%s", state.connectionErrorMsg.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Cerrar", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace SincroPro

// Main Program Entrypoint
int main(int argc, char** argv) {
    // 1. Initialise SyncEngine
    SincroPro::AppState state;
    state.syncEngine.loadConfig();
    
    // Start active background sessions
    state.syncEngine.startAll();

    // 2. Initialise GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    // Configure OpenGL context
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "SincroPro - Sincronizador Visual Multimotor C++", NULL, NULL);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable v-sync

    // 3. Initialise Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    
    // Customize fonts
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f);

    // Apply Style
    SincroPro::ApplyPremiumDarkTheme();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Load active session schema by default if exists
    if (!state.syncEngine.getSessions().empty()) {
        state.selectedSessionIdx = 0;
        const auto& s = state.syncEngine.getSessions()[0];
        strcpy(state.dbAConnStr, s.dbA_connStr.c_str());
        strcpy(state.dbBUrl, s.dbB_url.c_str());
        state.intervalSeconds = s.intervalSeconds;
        SincroPro::RefreshDatabaseSchemas(state);
    }

    // 4. Main Event/Render Loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Render SincroPro interface
        SincroPro::RenderUI(state);

        // Rendering commands
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // 5. Cleanup and Shutdown
    state.syncEngine.stopAll();
    
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
