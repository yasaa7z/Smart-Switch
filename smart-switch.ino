#include <ESPAsyncWebServer.h>
#include <ESPAsyncTCP.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

//pinout
#define Relay1 D5
#define Relay2 D6
#define LED_BUILTIN 2
#define RESET_BUTTON D1

//EEPROM
#define EEPROM_SIZE 512

//WiFi
String savedSSID = "";
String savedPassword = "";
bool wifiConfigured = false;
bool isAPMode = true;
const char* apSSID = "Smart Switch";
const char* apPassword = "12345678";
IPAddress apIP(192, 168, 4, 1);
IPAddress apNetMsk(255, 255, 255, 0);

//IP mode station
IPAddress local_IP(192, 168, 0, 10);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);

//Web server dan WebSocket
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

bool relay1Status = false;
bool relay2Status = false;

// ----------------- SETUP -----------------
void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(Relay1, OUTPUT);
  pinMode(Relay2, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  digitalWrite(Relay1, HIGH);
  digitalWrite(Relay2, HIGH);

  startAP();
  setupWebSocket();
  setupWebServer();
  server.begin();
}

// ----------------- LOOP -----------------
void loop() {
  //LED indikator
  static unsigned long lastBlink = 0;
  if (millis() - lastBlink > (isAPMode ? 1000 : 2000)) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    lastBlink = millis();
  }
  
  ws.cleanupClients(); //Bersihkan koneksi WebSocket yang tidak aktif
}

// ----------------- WEBSOCKET -----------------
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s (Total clients: %d)\n", 
                   client->id(), client->remoteIP().toString().c_str(), ws.count());
      // Kirim status relay saat ini ke client baru
      sendRelayStatus(client);
      break;
      
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected (Remaining clients: %d)\n", 
                   client->id(), ws.count()-1);
      break;
      
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len, client);
      break;
      
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len, AsyncWebSocketClient *client) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    
    data[len] = 0; //Null terminate string
    String message = (char*)data;
    
    //Parse JSON
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, message);
    
    String output = doc["output"];
    int state = doc["state"];
    
    Serial.printf("Received command from client #%u: %s = %d\n", client->id(), output.c_str(), state);
    
    bool statusChanged = false;
    
    if(output == "switch1") {
      bool newStatus = (state == 1);
      if(relay1Status != newStatus) {
        relay1Status = newStatus;
        digitalWrite(Relay1, relay1Status ? LOW : HIGH);
        statusChanged = true;
        Serial.printf("Switch 1 changed to: %s\n", relay1Status ? "ON" : "OFF");
      }
    } else if(output == "switch2") {
      bool newStatus = (state == 1);
      if(relay2Status != newStatus) {
        relay2Status = newStatus;
        digitalWrite(Relay2, relay2Status ? LOW : HIGH);
        statusChanged = true;
        Serial.printf("Switch 2 changed to: %s\n", relay2Status ? "ON" : "OFF");
      }
    }
    
    //Broadcast status update ke SEMUA client
    if(statusChanged) {
      broadcastRelayStatus();
      Serial.printf("Status broadcasted to %d clients\n", ws.count());
    }
  }
}

void sendRelayStatus(AsyncWebSocketClient *client) {
  DynamicJsonDocument doc(1024);
  doc["type"] = "status";
  doc["relay1"] = relay1Status;
  doc["relay2"] = relay2Status;
  doc["clientCount"] = ws.count();
  
  String response;
  serializeJson(doc, response);
  client->text(response);
  Serial.printf("Sending initial status to client #%u: %s\n", client->id(), response.c_str());
}

void broadcastRelayStatus() {
  DynamicJsonDocument doc(1024);
  doc["type"] = "status";
  doc["relay1"] = relay1Status;
  doc["relay2"] = relay2Status;
  doc["clientCount"] = ws.count();
  
  String response;
  serializeJson(doc, response);
  ws.textAll(response);
  Serial.printf("Broadcasting status to %d clients: %s\n", ws.count(), response.c_str());
}

void setupWebSocket() {
  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);
}

// ----------------- WEB SERVER -----------------
void startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID, apPassword);
  WiFi.softAPConfig(apIP, apIP, apNetMsk);
}

void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", pageWebUtama());
  });
}

// ----------------- HALAMAN HTML -----------------
String pageWebUtama() {
  return R"rawliteral(
<!DOCTYPE html>
<html lang="en">

<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Smart Switch</title>
    <style>
        *,
        *:before,
        *:after {
            -webkit-tap-highlight-color: rgba(0, 0, 0, 0);
            -webkit-tap-highlight-color: transparent;
        }

        body {
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: flex-start;
            height: 100vh;
            margin: 0;
            background-color: #5F6F65;
            font-family: 'Arial', sans-serif;
            position: relative;
            overflow-y: auto;
        }

        .container {
            text-align: center;
            height: auto;
            margin-top: 30px;
        }

        .header {
            font-size: 2.5em;
            font-weight: bold;
            color: #D4ECDD;
            text-shadow: 2px 2px 4px rgba(0, 0, 0, 0.2);
            margin-bottom: 30px;
        }

        .status-bar {
            background-color: #C9DABF;
            padding: 15px;
            border-radius: 10px;
            margin-bottom: 30px;
            color: #5F6F65;
            font-weight: bold;
        }

        .connection-status {
            background-color: #ff6b6b;
            color: white;
            padding: 10px;
            border-radius: 5px;
            margin-bottom: 20px;
            font-size: 0.9em;
        }

        .connection-status.connected {
            background-color: #51cf66;
        }

        .switch-container {
            display: grid;
            justify-items: center;
            grid-template-columns: repeat(1, 1fr);
            gap: 45px;
            height: 5rem;
        }

        .switch-box {
            background-color: #C9DABF;
            border-radius: 15px;
            padding: 40px;
            width: 5rem;
            box-shadow: 2px 2px 10px rgba(0, 0, 0, 0.1);
            display: flex;
            flex-direction: column;
            align-items: center;
        }

        .switch-label {
            margin-top: 25px;
            font-size: 1.2em;
            font-weight: bold;
            color: #5F6F65;
            text-shadow: 1px 1px 2px rgba(0, 0, 0, 0.1);
        }

        .switch {
            position: relative;
            display: inline-block;
            width: 80px;
            height: 44px;
        }

        .switch input {
            opacity: 0;
            width: 0;
            height: 0;
        }

        .slider {
            position: absolute;
            cursor: pointer;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background-color: grey;
            transition: .2s;
            border-radius: 44px;
        }

        .slider:before {
            position: absolute;
            content: "";
            height: 36px;
            width: 36px;
            left: 4px;
            bottom: 4px;
            background-color: white;
            transition: .2s;
            border-radius: 50%;
        }

        input:checked+.slider {
            background-color: #4caf50;
        }

        input:checked+.slider:before {
            transform: translateX(36px);
        }

        .slider.round {
            border-radius: 44px;
        }

        .slider.round:before {
            border-radius: 50%;
        }

        .config-link {
            margin-top: 20px;
        }

        .config-link a {
            color: #D4ECDD;
            text-decoration: none;
            font-size: 0.9em;
        }

        .navbar {
            position: fixed;
            bottom: 0;
            left: 0;
            right: 0;
            background-color: #C9DABF;
            padding: 5px;
            text-align: center;
            border-top-left-radius: 15px;
            border-top-right-radius: 15px;
        }

        .navbar a {
            color: #5F6F65;
            text-decoration: none;
            font-size: 1.3em;
            font-weight: bold;
        }
    </style>
</head>

<body>
    <div class="container">
        <div class="header">Smart Switch</div>
        <div class="connection-status" id="connectionStatus">
            Menghubungkan ke WebSocket...
        </div>
        <div class="status-bar">
            Kelompok JisJos Ni Boss - <span id="clientCount">Device terhubung: 0</span>
        </div>
        <div class="switch-container">
            <div class="switch-box">
                <label class="switch">
                    <input type="checkbox" onchange="toggleCheckbox(this)" id="switch1">
                    <span class="slider round"></span>
                </label>
                <div class="switch-label">Switch 1</div>
            </div>
            <div class="switch-box">
                <label class="switch">
                    <input type="checkbox" onchange="toggleCheckbox(this)" id="switch2">
                    <span class="slider round"></span>
                </label>
                <div class="switch-label">Switch 2</div>
            </div>
        </div>
        
        <script>
            let websocket;
            let reconnectInterval;

            function initWebSocket() {
                const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
                const wsUrl = protocol + '//' + window.location.host + '/ws';
                
                websocket = new WebSocket(wsUrl);
                
                websocket.onopen = function(event) {
                    console.log('WebSocket connected');
                    updateConnectionStatus(true);
                    clearInterval(reconnectInterval);
                };
                
                websocket.onclose = function(event) {
                    console.log('WebSocket disconnected');
                    updateConnectionStatus(false);
                    // Attempt to reconnect every 3 seconds
                    reconnectInterval = setInterval(function() {
                        console.log('Attempting to reconnect...');
                        initWebSocket();
                    }, 3000);
                };
                
                websocket.onerror = function(event) {
                    console.error('WebSocket error:', event);
                    updateConnectionStatus(false);
                };
                
                websocket.onmessage = function(event) {
                    console.log('Received message:', event.data);
                    const data = JSON.parse(event.data);
                    if (data.type === 'status') {
                        // Update UI tanpa trigger onChange event
                        updateSwitchStatus('switch1', data.relay1);
                        updateSwitchStatus('switch2', data.relay2);
                        
                        // Update client count
                        if (data.clientCount) {
                            document.getElementById('clientCount').textContent = 
                                'Device terhubung: ' + data.clientCount;
                        }
                        
                        console.log('Switch status updated - Switch1:', data.relay1, 'Switch2:', data.relay2);
                    }
                };
            }

            function updateConnectionStatus(connected) {
                const statusElement = document.getElementById('connectionStatus');
                if (connected) {
                    statusElement.textContent = 'Terhubung ke WebSocket';
                    statusElement.classList.add('connected');
                } else {
                    statusElement.textContent = 'Tidak terhubung - Mencoba menyambung kembali...';
                    statusElement.classList.remove('connected');
                }
            }

            function updateSwitchStatus(switchId, status) {
                const switchElement = document.getElementById(switchId);
                if (switchElement.checked !== status) {
                    switchElement.checked = status;
                    console.log(switchId + ' updated to:', status);
                }
            }

            function toggleCheckbox(element) {
                if (websocket.readyState === WebSocket.OPEN) {
                    const message = {
                        output: element.id,
                        state: element.checked ? 1 : 0
                    };
                    console.log('Sending command:', message);
                    websocket.send(JSON.stringify(message));
                } else {
                    console.error('WebSocket is not connected');
                    // Revert checkbox state if websocket is not connected
                    element.checked = !element.checked;
                    alert('Tidak dapat terhubung ke server. Silakan coba lagi.');
                }
            }

            // Initialize WebSocket connection when page loads
            document.addEventListener('DOMContentLoaded', function() {
                initWebSocket();
            });

            // Handle page visibility change to reconnect if needed
            document.addEventListener('visibilitychange', function() {
                if (!document.hidden && websocket.readyState !== WebSocket.OPEN) {
                    initWebSocket();
                }
            });
        </script>
    </div>
</body>
</html>
)rawliteral";
}
