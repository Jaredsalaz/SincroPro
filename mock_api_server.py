import json
from http.server import HTTPServer, BaseHTTPRequestHandler
import uuid

# In-memory mock database for DB B
DB_B = {
    "usuarios": [
        {"id": "guid_user_1", "nombre": "Mocker Alpha", "correo": "alpha@mock.com"},
        {"id": "guid_user_2", "nombre": "Mocker Beta", "correo": "beta@mock.com"}
    ],
    "productos": [
        {"id": "guid_prod_1", "descripcion": "Motor 150cc", "precio": "12500.00"},
        {"id": "guid_prod_2", "descripcion": "Casco Moto", "precio": "1500.00"}
    ],
    "ventas": []
}

class MockApiHandler(BaseHTTPRequestHandler):
    def _send_response(self, status, content_type, data):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, PUT, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()
        self.wfile.write(data.encode('utf-8'))

    def do_OPTIONS(self):
        self._send_response(200, "application/json", "")

    def do_GET(self):
        # Route schema fetch
        if self.path == "/api/schema":
            schema = [
                {
                    "table": "usuarios",
                    "columns": [
                        {"name": "id", "type": "string"},
                        {"name": "nombre", "type": "string"},
                        {"name": "correo", "type": "string"}
                    ]
                },
                {
                    "table": "productos",
                    "columns": [
                        {"name": "id", "type": "string"},
                        {"name": "descripcion", "type": "string"},
                        {"name": "precio", "type": "float"}
                    ]
                },
                {
                    "table": "ventas",
                    "columns": [
                        {"name": "id", "type": "string"},
                        {"name": "usuario_id", "type": "string"},
                        {"name": "producto_id", "type": "string"},
                        {"name": "cantidad", "type": "int"}
                    ]
                }
            ]
            self._send_response(200, "application/json", json.dumps(schema))
            return

        # Route query endpoints
        for table in DB_B.keys():
            if self.path == f"/api/{table}":
                self._send_response(200, "application/json", json.dumps(DB_B[table]))
                return

        self._send_response(404, "application/json", json.dumps({"error": "Endpoint not found"}))

    def do_POST(self):
        # Route insert endpoints
        content_length = int(self.headers['Content-Length'])
        post_data = self.rfile.read(content_length).decode('utf-8')
        
        try:
            payload = json.loads(post_data)
        except Exception as e:
            self._send_response(400, "application/json", json.dumps({"error": "Invalid JSON body"}))
            return

        for table in DB_B.keys():
            if self.path == f"/api/{table}":
                # Generate new GUID/UUID for target primary key
                new_id = str(uuid.uuid4())
                payload["id"] = new_id
                DB_B[table].append(payload)
                
                print(f"[API SERVER] Inserted row into '{table}': {payload}")
                self._send_response(201, "application/json", json.dumps({"id": new_id}))
                return

        self._send_response(404, "application/json", json.dumps({"error": "Endpoint not found"}))

    def do_PUT(self):
        # Route update endpoints /api/{table}/{id}
        parts = self.path.strip("/").split("/")
        if len(parts) == 3 and parts[0] == "api":
            table, row_id = parts[1], parts[2]
            
            if table in DB_B:
                content_length = int(self.headers['Content-Length'])
                put_data = self.rfile.read(content_length).decode('utf-8')
                
                try:
                    payload = json.loads(put_data)
                except Exception as e:
                    self._send_response(400, "application/json", json.dumps({"error": "Invalid JSON body"}))
                    return

                # Find the row to update
                found = False
                for row in DB_B[table]:
                    if row.get("id") == row_id:
                        row.update(payload)
                        # Keep ID correct
                        row["id"] = row_id
                        found = True
                        break

                if found:
                    print(f"[API SERVER] Updated row in '{table}' id={row_id}: {payload}")
                    self._send_response(200, "application/json", json.dumps({"success": True}))
                else:
                    # If not found, append it
                    payload["id"] = row_id
                    DB_B[table].append(payload)
                    print(f"[API SERVER] Created row in '{table}' id={row_id} via PUT: {payload}")
                    self._send_response(200, "application/json", json.dumps({"success": True}))
                return

        self._send_response(404, "application/json", json.dumps({"error": "Endpoint not found"}))

def run(port=5000):
    server_address = ('', port)
    httpd = HTTPServer(server_address, MockApiHandler)
    print(f"[API SERVER] Mock API server running on port {port}...")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    print("[API SERVER] Shutting down.")

if __name__ == '__main__':
    run()
