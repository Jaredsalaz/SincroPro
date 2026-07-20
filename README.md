# SincroPro - Sincronizador Visual Multimotor C++

SincroPro es una aplicación en C++ diseñada para la sincronización y mapeo visual de datos entre diferentes motores de bases de datos (específicamente Mysonda V1 en MySQL y Mysonda V2 en SQL Server) mediante la integración de **endpoints de API puros**.

Esto permite realizar la sincronización entre bases de datos de forma segura, sin exponer credenciales de acceso directo a datos dentro de la aplicación cliente.

---

## Arquitectura del Proyecto

El sistema se divide en dos capas principales:

1. **Cliente C++ (SincroPro):**
   * **GUI en Dear ImGui:** Interfaz premium con diseño glassmorphic y mapeador visual Bézier interactivo de columnas.
   * **SyncEngine:** Motor de sincronización en segundo plano con control de concurrencia e hilos por sesión.
   * **Polimorfismo de Conectores:** Instanciación dinámica basada en protocolo (utiliza HTTP API mediante `APIConnector` o ODBC directo mediante `ODBCConnector`).

2. **Capa de APIs de Base de Datos:**
   * Servidores HTTP ligeros en Python que conectan con los motores locales/remotos y exponen esquemas y operaciones CRUD de manera uniforme.

---

## Estructura de Directorios

* `src/`: Código fuente de la aplicación C++ (Lógica del motor y GUI).
* `API/`: Carpeta contenedora de las APIs de conexión.
  > [!NOTE]
  > Por razones de seguridad y para evitar la exposición de credenciales críticas de producción (contraseñas de MySQL y SQL Server), los archivos de scripts de esta carpeta no se suben al control de versiones de Git.
  > 
  > Aquí se deben crear los archivos:
  > * `API/Mysonda/mysondav1_api.py` (API de MySQL)
  > * `API/Mysonda/mysondav2_api.py` (API de SQL Server)
  > * `API/Mysonda/run_apis.bat` (Script de ejecución masiva)
* `mock_api_server.py`: Servidor de pruebas con base de datos simulada en memoria.
* `CMakeLists.txt`: Archivo de configuración de construcción de CMake.

---

## Compilación y Ejecución (C++)

### Requisitos:
* Compilador compatible con C++17 (ej. Visual Studio MSBuild, GCC, Clang).
* CMake 3.15 o superior.

### Pasos para compilar en Windows (Release):
```bash
cmake -B build
cmake --build build --config Release
```

El ejecutable compilado estará ubicado en `build/bin/Release/SincroPro.exe`.
