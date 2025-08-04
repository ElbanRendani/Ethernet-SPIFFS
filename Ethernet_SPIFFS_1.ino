/*
 * RFID Access Control System with Web Interface
 * 
 * Features:
 * - Non-blocking RFID card scanning
 * - Web interface for card management (add/delete/toggle)
 * - GPIO output activation for valid cards (GPIO4)
 * - Automatic redirect after form submission
 * - Secure file operations with error handling
 * - Buffer overflow protection
 * - LCD 2x16 I2C display
 * - Buzzer and LED feedback
 * 
 * Pin Configuration:
 * - PN532 RFID (UART2): RX=16, TX=17
 * - W5500 Ethernet: SCK=18, MISO=19, MOSI=23, CS=5
 * - Output Pin: GPIO4 
 * - LCD I2C: SDA=21, SCL=22
 * - Buzzer: GPIO25
 * - LED: GPIO26
 * 
 * Network:
 * - Static IP: 192.168.1.177
 * - MAC: DE:AD:BE:EF:FE:ED
 */

#include <SPI.h>
#include <Ethernet.h>
#include "FS.h"
#include "SPIFFS.h"
#include <Adafruit_PN532.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Configuration Constants
#define MAX_ROWS 200
#define MAX_REQUEST_LENGTH 512
#define OUTPUT_PIN 4         // Main access control output
#define BUZZER_PIN 25        // Buzzer pin
#define LED_PIN 26           // LED pin
#define RFID_CHECK_INTERVAL 100  // ms between RFID checks

// LCD Configuration
LiquidCrystal_I2C lcd(0x27, 16, 2); // I2C address 0x27, 16 chars, 2 lines

String fileRows[MAX_ROWS];
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress ip(192, 168, 1, 177);
EthernetServer server(80);

HardwareSerial PN532Serial(2);
Adafruit_PN532 nfc(PN532Serial);

String String_ID = "";
const char* dataPath = "/data.txt";

// Web server variables
char currentRequest[MAX_REQUEST_LENGTH];
unsigned currentRequestPos = 0;
EthernetClient client;
unsigned long lastClientCheck = 0;
const unsigned long clientTimeout = 1000;

// RFID timing variables
unsigned long lastRfidCheck = 0;

void setup() {
  Serial.begin(115200);
  
  // Initialize output pins
  pinMode(OUTPUT_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(OUTPUT_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print("Tempel Kartu");

  // Initialize RFID reader
  PN532Serial.begin(115200, SERIAL_8N1, 16, 17);
  if (!nfc.begin()) {
    Serial.println("❌ Failed to initialize PN532");
    lcd.clear();
    lcd.print("RFID Error!");
    while (1);
  }
  
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("❌ Didn't find PN532");
    lcd.clear();
    lcd.print("No RFID Reader");
    while (1);
  }
  
  nfc.SAMConfig();
  Serial.println("✅ PN532 Ready");

  // Initialize filesystem
  if (!SPIFFS.begin(true)) {
    Serial.println("❌ Failed to mount SPIFFS");
    lcd.clear();
    lcd.print("FS Error!");
    while (1);
  }

  // Initialize network
  SPI.begin(18, 19, 23, 5);
  Ethernet.init(5);
  

  // Ip nya static
  // if (Ethernet.begin(mac) == 0) {
  //   Serial.println("⚠️ Failed to configure Ethernet using DHCP");
  //   // Fall back to static IP
    Ethernet.begin(mac, ip);
  // }
  
  server.begin();
  Serial.print("💻 Server started at ");
  Serial.println(Ethernet.localIP());

  // Initialize data file
  if (!SPIFFS.exists(dataPath)) {
    File f = SPIFFS.open(dataPath, FILE_WRITE);
    if (!f) {
      Serial.println("❌ Failed to create data file");
    } else {
      f.close();
    }
  }

  // Load initial data
  if (!readFile(SPIFFS, dataPath, fileRows, MAX_ROWS)) {
    Serial.println("⚠️ Error reading initial data");
  }
}

void loop() {
  handleNetwork();
  handleRFID();
}

void handleNetwork() {
  // Handle client connections with timeout
  if (!client || !client.connected()) {
    client = server.available();
    currentRequestPos = 0;
    memset(currentRequest, 0, MAX_REQUEST_LENGTH);
    lastClientCheck = millis();
    return;
  }

  // Read client data with buffer protection
  while (client.available() && currentRequestPos < MAX_REQUEST_LENGTH - 1) {
    char c = client.read();
    currentRequest[currentRequestPos++] = c;

    // Check for end of request
    if (currentRequestPos >= 4 && 
        strncmp(&currentRequest[currentRequestPos-4], "\r\n\r\n", 4) == 0) {
      handleRequest(client, String(currentRequest));
      client.stop();
      return;
    }
  }

  // Client timeout
  if (millis() - lastClientCheck > clientTimeout) {
    client.stop();
    Serial.println("⌛ Client timeout");
  }
}

void handleRequest(EthernetClient &client, String request) {
  Serial.println("Request diterima:");
  Serial.println(request);

  if (request.startsWith("GET /")) {
    if (request.indexOf("GET /delete?id=") != -1) {
      // Handle delete request
      int start = request.indexOf("id=") + 3;
      int end = request.indexOf(" ", start);
      String idToDelete = request.substring(start, end);
      idToDelete = urlDecode(idToDelete);
      deleteRowById(idToDelete);
      
      // Redirect ke halaman utama setelah delete
      sendRedirect(client, "/");
      return;
    }
    else if (request.indexOf("GET /toggle?id=") != -1) {
      // Handle toggle enable request
      int start = request.indexOf("id=") + 3;
      int end = request.indexOf(" ", start);
      String idToToggle = request.substring(start, end);
      idToToggle = urlDecode(idToToggle);
      toggleEnable(idToToggle);
      
      // Redirect ke halaman utama setelah toggle
      sendRedirect(client, "/");
      return;
    }
    else if (request.indexOf("GET /deleteall") != -1) {
      // Handle delete all request
      eraseAllData();
      
      // Redirect ke halaman utama setelah delete all
      sendRedirect(client, "/");
      return;
    }
    else if (request.indexOf("GET /add?") != -1) {
      // Handle add request
      int idStart = request.indexOf("id=") + 3;
      int idEnd = request.indexOf("&", idStart);
      String id = request.substring(idStart, idEnd);
      
      int nameStart = request.indexOf("name=") + 5;
      int nameEnd = request.indexOf("&", nameStart);
      String name = nameEnd == -1 ? request.substring(nameStart) : request.substring(nameStart, nameEnd);
      name = urlDecode(name);
      
      int unitStart = request.indexOf("unit=") + 5;
      int unitEnd = request.indexOf("&", unitStart);
      String unit = unitEnd == -1 ? request.substring(unitStart) : request.substring(unitStart, unitEnd);
      unit = urlDecode(unit);
      
      String enable = "1";
      writeData(id, name, unit, enable);
      
      // Redirect ke halaman utama setelah add
      sendRedirect(client, "/");
      return;
    }
    else if (request.indexOf("GET /") != -1) {
      // Default page handler
      sendHTML(client);
      return;
    }
  }
}

void sendRedirect(EthernetClient &client, String location) {
  client.println("HTTP/1.1 303 See Other");
  client.print("Location: ");
  client.println(location);
  client.println("Connection: close");
  client.println();
}

void handleRFID() {
  // Non-blocking RFID check
  if (millis() - lastRfidCheck > RFID_CHECK_INTERVAL) {
    lastRfidCheck = millis();
    
    uint8_t uid[7];
    uint8_t uidLength;
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 0)) {
      Serial.print("📇 Card detected: ");
      ConvertByteToString(uid, uidLength);
      Serial.println(String_ID);
      
      if (checkCardAccess(String_ID)) {
        Serial.println("✅ Access granted - Activating output");
        grantAccess(String_ID);
      } else {
        Serial.println("❌ Access denied");
        accessDenied();
      }
    }
  }
}

void grantAccess(String cardId) {
  // Activate main output
  digitalWrite(OUTPUT_PIN, HIGH);
  
  // Find card owner name
  String ownerName = "Unknown";
  for (int i = 0; i < MAX_ROWS; i++) {
    if (fileRows[i].startsWith(cardId + "|")) {
      int firstPipe = fileRows[i].indexOf('|');
      int secondPipe = fileRows[i].indexOf('|', firstPipe + 1);
      ownerName = fileRows[i].substring(firstPipe + 1, secondPipe);
      break;
    }
  }
  
  // Update LCD
  lcd.clear();
  lcd.print("Access Granted");
  lcd.setCursor(0, 1);
  lcd.print(ownerName);
  
  // Activate buzzer and LED
  digitalWrite(BUZZER_PIN, HIGH);
  digitalWrite(LED_PIN, HIGH);
  delay(200);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
  delay(200);
  digitalWrite(BUZZER_PIN, HIGH);
  digitalWrite(LED_PIN, HIGH);
  delay(200);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
  
  // Keep main output active for 1 second
  delay(600); // Remaining 600ms of the 1 second total
  digitalWrite(OUTPUT_PIN, LOW);
  
  // Reset LCD after 2 seconds
  delay(1000);
  lcd.clear();
  lcd.print("Tempel Kartu");
}

void accessDenied() {
  // Update LCD
  lcd.clear();
  lcd.print("Access Denied!");
  lcd.setCursor(0, 1);
  lcd.print("Unauthorized");
  
  // Activate buzzer and LED
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
  
  // Reset LCD after 2 seconds
  delay(1500);
  lcd.clear();
  lcd.print("Tempel Kartu");
}

bool checkCardAccess(String cardId) {
  for (int i = 0; i < MAX_ROWS; i++) {
    if (fileRows[i].length() > 0) {
      int firstPipe = fileRows[i].indexOf('|');
      if (firstPipe != -1) {
        String id = fileRows[i].substring(0, firstPipe);
        if (id == cardId) {
          int thirdPipe = fileRows[i].lastIndexOf('|');
          if (thirdPipe != -1) {
            String enable = fileRows[i].substring(thirdPipe + 1);
            return (enable == "1");
          }
        }
      }
    }
  }
  return false;
}

void ConvertByteToString(byte *ID, uint8_t length) {
  String_ID = "";
  for (uint8_t i = 0; i < length; i++) {
    if (ID[i] < 0x10) String_ID += "0";
    String_ID += String(ID[i], HEX);
  }
  String_ID.toUpperCase();
}

bool readFile(fs::FS &fs, const char *path, String *Return, int arrayLength) {
  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.println("❌ Failed to open file for reading");
    return false;
  }

  try {
    int i = 0;
    while (file.available() && i < arrayLength) {
      String line = file.readStringUntil('\n');
      line.trim();
      if (line.length() > 0) {
        Return[i++] = line;
      }
    }
    file.close();
    return true;
  } catch (...) {
    Serial.println("⚠️ Exception while reading file");
    file.close();
    return false;
  }
}

bool writeData(String id, String nama, String unit, String enable) {
  String data = id + "|" + nama + "|" + unit + "|" + enable;
  
  File file = SPIFFS.open(dataPath, FILE_APPEND);
  if (!file) {
    Serial.println("❌ Failed to open file for writing");
    return false;
  }

  try {
    file.println(data);
    file.close();
    
    // Update in-memory data
    for (int i = 0; i < MAX_ROWS; i++) {
      if (fileRows[i].length() == 0) {
        fileRows[i] = data;
        break;
      }
    }
    return true;
  } catch (...) {
    Serial.println("⚠️ Exception while writing file");
    file.close();
    return false;
  }
}

void deleteRowById(String targetId) {
  File tempFile = SPIFFS.open("/temp.txt", FILE_WRITE);
  if (!tempFile) {
    Serial.println("❌ Failed to create temp file");
    return;
  }

  File originalFile = SPIFFS.open(dataPath, FILE_READ);
  if (!originalFile) {
    Serial.println("❌ Failed to open data file");
    tempFile.close();
    return;
  }

  while (originalFile.available()) {
    String line = originalFile.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      int pipePos = line.indexOf('|');
      if (pipePos != -1) {
        String id = line.substring(0, pipePos);
        if (id != targetId) {
          tempFile.println(line);
        }
      }
    }
  }

  originalFile.close();
  tempFile.close();

  SPIFFS.remove(dataPath);
  SPIFFS.rename("/temp.txt", dataPath);

  // Update in-memory data
  for (int i = 0; i < MAX_ROWS; i++) {
    if (fileRows[i].length() > 0) {
      int pipePos = fileRows[i].indexOf('|');
      if (pipePos != -1) {
        String id = fileRows[i].substring(0, pipePos);
        if (id == targetId) {
          fileRows[i] = "";
        }
      }
    }
  }
}

void toggleEnable(String targetId) {
  File tempFile = SPIFFS.open("/temp.txt", FILE_WRITE);
  if (!tempFile) {
    Serial.println("❌ Failed to create temp file");
    return;
  }

  File originalFile = SPIFFS.open(dataPath, FILE_READ);
  if (!originalFile) {
    Serial.println("❌ Failed to open data file");
    tempFile.close();
    return;
  }

  while (originalFile.available()) {
    String line = originalFile.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      int firstPipe = line.indexOf('|');
      if (firstPipe != -1) {
        String id = line.substring(0, firstPipe);
        if (id == targetId) {
          int secondPipe = line.indexOf('|', firstPipe + 1);
          int thirdPipe = line.indexOf('|', secondPipe + 1);
          if (secondPipe != -1 && thirdPipe != -1) {
            String nama = line.substring(firstPipe + 1, secondPipe);
            String unit = line.substring(secondPipe + 1, thirdPipe);
            String enable = line.substring(thirdPipe + 1);
            
            enable = (enable == "1") ? "0" : "1";
            tempFile.println(id + "|" + nama + "|" + unit + "|" + enable);
            
            // Update in-memory data
            for (int i = 0; i < MAX_ROWS; i++) {
              if (fileRows[i].startsWith(id + "|")) {
                fileRows[i] = id + "|" + nama + "|" + unit + "|" + enable;
                break;
              }
            }
            continue;
          }
        }
      }
      tempFile.println(line);
    }
  }

  originalFile.close();
  tempFile.close();

  SPIFFS.remove(dataPath);
  SPIFFS.rename("/temp.txt", dataPath);
}

void eraseAllData() {
  File file = SPIFFS.open(dataPath, FILE_WRITE);
  if (!file) {
    Serial.println("❌ Failed to open file for erase");
    return;
  }
  
  file.print("");
  file.close();
  
  for (int i = 0; i < MAX_ROWS; i++) {
    fileRows[i] = "";
  }
}

String urlDecode(String input) {
  String decoded = "";
  char temp[] = "0x00";
  unsigned int len = input.length();
  
  for (unsigned int i = 0; i < len; i++) {
    if (input[i] == '%') {
      if (i + 2 < len) {
        temp[2] = input[i + 1];
        temp[3] = input[i + 2];
        decoded += (char)strtol(temp, NULL, 16);
        i += 2;
      }
    } else if (input[i] == '+') {
      decoded += ' ';
    } else {
      decoded += input[i];
    }
  }
  
  return decoded;
}

void sendHTML(EthernetClient &client, String msg = "") {
  // Baca ulang data dari file untuk memastikan tampilan terkini
  readFile(SPIFFS, dataPath, fileRows, MAX_ROWS);
  
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  
  client.println("<!DOCTYPE html>");
  client.println("<html lang='en'>");
  client.println("<head>");
  client.println("<meta charset='UTF-8'>");
  client.println("<meta name='viewport' content='width=device-width, initial-scale=1.0'>");
  client.println("<title>RFID Data Management</title>");
  client.println("<style>");
  client.println("body { font-family: Arial, sans-serif; margin: 20px; background-color: #121212; color: #e0e0e0; }");
  client.println("h1 { color: #bb86fc; }");
  client.println(".container { max-width: 1000px; margin: 0 auto; }");
  client.println(".message { padding: 10px; margin: 10px 0; background-color: #333; border-radius: 5px; color: #fff; }");
  client.println("table { width: 100%; border-collapse: collapse; margin: 20px 0; }");
  client.println("th, td { border: 1px solid #444; padding: 12px; text-align: left; }");
  client.println("th { background-color: #1f1f1f; color: #bb86fc; }");
  client.println("tr:nth-child(even) { background-color: #1e1e1e; }");
  client.println("tr:nth-child(odd) { background-color: #2a2a2a; }");
  client.println("tr:hover { background-color: #333; }");
  client.println(".form-group { margin-bottom: 15px; }");
  client.println("label { display: block; margin-bottom: 5px; color: #bb86fc; }");
  client.println("input[type='text'] { width: 100%; padding: 8px; box-sizing: border-box; background-color: #333; color: #fff; border: 1px solid #444; border-radius: 4px; }");
  client.println("button { padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; font-weight: bold; margin-right: 5px; }");
  client.println(".add-btn { background-color: #03dac6; color: #000; }");
  client.println(".add-btn:hover { background-color: #018786; }");
  client.println(".delete-btn { background-color: #cf6679; color: #000; }");
  client.println(".delete-btn:hover { background-color: #b00020; }");
  client.println(".delete-all-btn { background-color: #ff7043; color: #000; }");
  client.println(".delete-all-btn:hover { background-color: #bf360c; }");
  client.println(".toggle-btn { background-color: #bb86fc; color: #000; }");
  client.println(".toggle-btn:hover { background-color: #3700b3; }");
  client.println(".enabled { color: #03dac6; font-weight: bold; }");
  client.println(".disabled { color: #cf6679; font-weight: bold; }");
  client.println("</style>");
  client.println("</head>");
  client.println("<body>");
  client.println("<div class='container'>");
  client.println("<h1>RFID Data Management</h1>");
  
  if (msg.length() > 0) {
    client.println("<div class='message'>" + msg + "</div>");
  }
  
  client.println("<h2>Tambah Data Baru</h2>");
  client.println("<form action='/add' method='get'>");
  client.println("<div class='form-group'>");
  client.println("<label for='id'>UID RFID:</label>");
  client.println("<input type='text' id='id' name='id' value='" + String_ID + "' required>");
  client.println("</div>");
  client.println("<div class='form-group'>");
  client.println("<label for='name'>Nama:</label>");
  client.println("<input type='text' id='name' name='name' required>");
  client.println("</div>");
  client.println("<div class='form-group'>");
  client.println("<label for='unit'>Unit:</label>");
  client.println("<input type='text' id='unit' name='unit' required>");
  client.println("</div>");
  client.println("<button type='submit' class='add-btn'>Tambah Data</button>");
  client.println("</form>");
  
  client.println("<h2>Data RFID</h2>");
  client.println(generateHTMLTable());
  
  client.println("<form action='/deleteall' method='get' onsubmit='return confirm(\"Apakah Anda yakin ingin menghapus semua data?\")'>");
  client.println("<button type='submit' class='delete-all-btn'>Hapus Semua Data</button>");
  client.println("</form>");
  
  client.println("</div>");
  client.println("</body>");
  client.println("</html>");
}

String generateHTMLTable() {
  String table = "<table>";
  table += "<thead><tr><th>UID RFID</th><th>Nama</th><th>Unit</th><th>Status</th><th>Aksi</th></tr></thead>";
  table += "<tbody>";
  
  for (int i = 0; i < MAX_ROWS; i++) {
    if (fileRows[i].length() > 0) {
      int firstPipe = fileRows[i].indexOf('|');
      int secondPipe = fileRows[i].indexOf('|', firstPipe + 1);
      int thirdPipe = fileRows[i].indexOf('|', secondPipe + 1);
      
      if (firstPipe != -1 && secondPipe != -1 && thirdPipe != -1) {
        String id = fileRows[i].substring(0, firstPipe);
        String name = fileRows[i].substring(firstPipe + 1, secondPipe);
        String unit = fileRows[i].substring(secondPipe + 1, thirdPipe);
        String enable = fileRows[i].substring(thirdPipe + 1);
        
        table += "<tr>";
        table += "<td>" + id + "</td>");
        table += "<td>" + name + "</td>");
        table += "<td>" + unit + "</td>");
        
        table += "<td>";
        if (enable == "1") {
          table += "<span class='enabled'>ENABLED</span>";
        } else {
          table += "<span class='disabled'>DISABLED</span>";
        }
        table += "</td>";
        
        table += "<td>";
        table += "<form action='/toggle' method='get' style='display: inline;'>";
        table += "<input type='hidden' name='id' value='" + id + "'>";
        table += "<button type='submit' class='toggle-btn'>Toggle</button>";
        table += "</form>";
        
        table += "<form action='/delete' method='get' onsubmit='return confirm(\"Apakah Anda yakin ingin menghapus data ini?\")' style='display: inline;'>";
        table += "<input type='hidden' name='id' value='" + id + "'>";
        table += "<button type='submit' class='delete-btn'>Hapus</button>";
        table += "</form>";
        table += "</td>";
        
        table += "</tr>";
      }
    }
  }
  
  table += "</tbody></table>";
  return table;
}