#include <SPI.h>
#include <Ethernet.h>
#include "FS.h"
#include "SPIFFS.h"

// Ethernet config
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress ip(192, 168, 1, 177);
EthernetServer server(80);

// Konfigurasi pin SPI
#define PIN_SCK  18
#define PIN_MISO 19
#define PIN_MOSI 23
#define PIN_CS   5

// Path file
const char* dataPath = "/data.txt";




// Global state
String currentRequest = "";
EthernetClient client;
unsigned long lastClientCheck = 0;
const unsigned long clientTimeout = 1000;


/* 
**NOTE**
VARIABLE GLOBAL
Tujuan mendeklarasikan Variable di bagian paling atas sebelum fungsi setup adalah untuk menjadikan variable ini sebagai variable global
artinyua dia dapat di akses di semua Fungsi yang di deklarasikan di bawah, Jika sebuah variable di deklarasikan di dalam fungsi dia akan 
di anggap sebagai variable lokal dan hanya akan terbaca pada fungsi itu saja
*/


// Function declarations
void handleRequest(EthernetClient &client, String request);
void sendHTML(EthernetClient &client, String msg = "");
void writeData(String id, String nama, String unit);
void eraseAllData();
void deleteRowById(String targetId);
String urlDecode(String input);

void setup() {
  Serial.begin(115200);

  /*
  
  **NOTE**
  Pada project yang menggunakan ESP32 harus di deklarasikan lagi pin SPI nya

  */
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
  Ethernet.init(PIN_CS);

  if (!SPIFFS.begin(true)) {
    Serial.println("Gagal mount SPIFFS");
    return;
  }


    /*
  
  **NOTE**
  pada Project ini Hanya MAC dan IP yang di atur secara statis, sisanya di atur oleh DHCP

  */
  Ethernet.begin(mac, ip);
  delay(1000);
  server.begin();
  Serial.print("Server aktif di: ");
  Serial.println(Ethernet.localIP());

  if (!SPIFFS.exists(dataPath)) {
    File f = SPIFFS.open(dataPath, FILE_WRITE);
    f.close();
  }
}

void loop() {
  if (!client || !client.connected()) {
    client = server.available();
    currentRequest = "";
    lastClientCheck = millis();
    return;
  }

  while (client.available()) {
    char c = client.read();
    currentRequest += c;
    if (currentRequest.endsWith("\r\n\r\n")) {
      handleRequest(client, currentRequest);
      client.stop();
      return;
    }
  }


  
  if (millis() - lastClientCheck > clientTimeout) {
    client.stop();
    Serial.println("Client timeout");
  }
}










  /*
  
 ================================================ **NOTE** ==========================================================================

  
Fungsi di bawah ini bertujuan untuk membaca dan memecahkan URL kemudian di masukan sebagai variable yang akan di proses lagi,
di dalam sini terdapat fungsi urlDecode(String input);
gunanya adalah untuk menghapus tanda + di dalam URL dan menggantinya dengan spasi (in case dalam data yang dimasukan terdapat spasi)
Input     : Abang Tio
URL       : Abang+Tio
Variable  : Abang Tio

====================================================================================================================================
  */

void handleRequest(EthernetClient &client, String request) {
  Serial.println("Memproses request:");
  Serial.println(request);

  if (request.indexOf("GET /erase") >= 0) {
    eraseAllData();
    sendHTML(client, "Semua data berhasil dihapus.");
  } 
  else if (request.indexOf("GET /delete?id=") >= 0) {
    int idIndex = request.indexOf("id=") + 3;
    String id = request.substring(idIndex, request.indexOf(" ", idIndex));
    id = urlDecode(id);
    deleteRowById(id);
    sendHTML(client, "Data berhasil dihapus.");
  } 
  else if (request.indexOf("GET /add?") >= 0) {
    int i1 = request.indexOf("id=") + 3;
    int i2 = request.indexOf("&nama=");
    int i3 = request.indexOf("&unit=");
    String id = urlDecode(request.substring(i1, i2));
    String nama = urlDecode(request.substring(i2 + 6, i3));
    String unit = urlDecode(request.substring(i3 + 6, request.indexOf(" ", i3)));
    writeData(id, nama, unit);
    sendHTML(client, "Data berhasil ditambahkan.");
  } 
  else {
    sendHTML(client);
  }
}

void sendHTML(EthernetClient &client, String msg) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println("<!DOCTYPE html><html><head><title>Elban Rendani</title>");
  client.println("<style>");
  client.println("body { background-color: #121212; color: #f5f5f5; font-family: sans-serif; padding: 20px; text-align: center; }");
  client.println("table { width: 100%; border-collapse: collapse; margin-top: 20px; }");
  client.println("th, td { padding: 12px; border: 1px solid #333; text-align: center; }");
  client.println("th { background-color: #222; color: #fff; }");
  client.println("tr:nth-child(even) { background-color: #1e1e1e; }");
  client.println("tr:hover { background-color: #2c2c2c; }");
  client.println("input, button { padding: 8px; background-color: #333; color: white; border: none; margin-top: 5px; }");
  client.println("a { color: #4FC3F7; text-decoration: none; }");
  client.println("a:hover { text-decoration: underline; }");
  client.println("</style></head><body>");

  client.println("<h2>Elban Rendani</h2>");
  client.println("<p><i>Register RFID 1</i></p>");
  if (msg != "") client.println("<p><b>" + msg + "</b></p>");

  client.println("<table><tr><th>ID</th><th>Nama</th><th>Enable</th><th>Aksi</th></tr>");
  File file = SPIFFS.open(dataPath);
  while (file.available()) {
    String line = file.readStringUntil('\n');
    if (line.length() > 0) {
      int p1 = line.indexOf(',');
      int p2 = line.lastIndexOf(',');
      String id = line.substring(0, p1);
      String nama = line.substring(p1 + 1, p2);
      String unit = line.substring(p2 + 1);
      client.print("<tr><td>" + id + "</td><td>" + nama + "</td><td>" + unit + "</td>");
      client.print("<td><a href='/delete?id=" + id + "'>Hapus</a></td></tr>");
    }
  }
  file.close();
  client.println("</table>");

  client.println("<h3>Tambah Data</h3>");
  client.println("<form action='/add' method='GET'>");
  client.println("ID: <input type='text' name='id'><br>");
  client.println("Nama: <input type='text' name='nama'><br>");
  client.println("Enable: <input type='text' name='unit'><br>");
  client.println("<input type='submit' value='Tambah'>");
  client.println("</form>");
  client.println("<br><a href='/erase'><button>Hapus Semua Data</button></a>");
  client.println("</body></html>");
}

void writeData(String id, String nama, String unit) {
  File file = SPIFFS.open(dataPath, FILE_APPEND);
  if (!file) {
    Serial.println("Gagal menulis ke file");
    return;
  }
  file.println(id + "," + nama + "," + unit);
  file.close();
}

void eraseAllData() {
  SPIFFS.remove(dataPath);
  File f = SPIFFS.open(dataPath, FILE_WRITE);
  f.close();
}

void deleteRowById(String targetId) {
  File file = SPIFFS.open(dataPath, FILE_READ);
  String newData = "";
  while (file.available()) {
    String line = file.readStringUntil('\n');
    if (line.length() > 0) {
      String idStr = line.substring(0, line.indexOf(','));
      if (idStr != targetId) {
        newData += line + "\n";
      }
    }
  }
  file.close();

  File writeFile = SPIFFS.open(dataPath, FILE_WRITE);
  writeFile.print(newData);
  writeFile.close();
}

String urlDecode(String input) {
  String decoded = "";
  char c;
  for (int i = 0; i < input.length(); i++) {
    c = input[i];
    if (c == '+') {
      decoded += ' ';
    } else if (c == '%') {
      String hex = input.substring(i + 1, i + 3);
      decoded += (char) strtol(hex.c_str(), NULL, 16);
      i += 2;
    } else {
      decoded += c;
    }
  }
  return decoded;
}

