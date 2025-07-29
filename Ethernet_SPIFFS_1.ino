#include <SPI.h>
#include <Ethernet.h>  // Gunakan Ethernet_Generic jika perlu
#include "FS.h"
#include "SPIFFS.h"

// Konfigurasi Ethernet
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress ip(192, 168, 1, 177);  // Static fallback IP
EthernetServer server(80);

// Pin SPI (VSPI)
#define PIN_SCK  18
#define PIN_MISO 19
#define PIN_MOSI 23
#define PIN_CS   5  // Wajib disesuaikan dengan wiring

// Path file di SPIFFS
const char* dataPath = "/data.txt";

//////////////////////////
//  FUNGSI PENDUKUNG
//////////////////////////

void sendHTML(EthernetClient &client, String msg = "") {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println("<!DOCTYPE html><html><head><title>Data SPIFFS</title></head><body>");
  client.println("<h2>Data Tersimpan</h2>");

  if (msg != "") client.println("<p><b>" + msg + "</b></p>");

  client.println("<table border='1'><tr><th>ID</th><th>Nama</th><th>Unit</th><th>Aksi</th></tr>");

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

  // Form tambah data
  client.println("<h3>Tambah Data</h3>");
  client.println("<form action='/add' method='GET'>");
  client.println("ID: <input type='text' name='id'><br>");
  client.println("Nama: <input type='text' name='nama'><br>");
  client.println("Unit: <input type='text' name='unit'><br>");
  client.println("<input type='submit' value='Tambah'>");
  client.println("</form>");

  // Tombol hapus semua
  client.println("<br><a href='/erase'><button>Hapus Semua Data</button></a>");
  client.println("</body></html>");
}

void writeData(String id, String nama, String unit) {
  File file = SPIFFS.open(dataPath, FILE_APPEND);
  if (!file) {
    Serial.println("Gagal menulis file");
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

//////////////////////////
//  SETUP
//////////////////////////

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Inisialisasi SPI
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
  Ethernet.init(PIN_CS);

  if (!SPIFFS.begin(true)) {
    Serial.println("Gagal mount SPIFFS");
    return;
  }

  // DHCP terlebih dahulu
  // if (Ethernet.begin(mac) == 0) {
  //   Serial.println("DHCP gagal, coba static IP...");
  //   Ethernet.begin(mac, ip);
  // }

  Ethernet.begin(mac, ip);

  delay(1000);
  server.begin();
  Serial.print("Server IP: ");
  Serial.println(Ethernet.localIP());

  // Buat file jika belum ada
  if (!SPIFFS.exists(dataPath)) {
    File f = SPIFFS.open(dataPath, FILE_WRITE);
    f.close();
  }
}

//////////////////////////
//  LOOP UTAMA
//////////////////////////

void loop() {
  EthernetClient client = server.available();

  if (client) {
    Serial.println("Client connected");
    String request = "";
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        request += c;
        if (c == '\n' && request.endsWith("\r\n\r\n")) break;
      }
    }

    // Routing berdasarkan URL
    if (request.indexOf("GET /erase") >= 0) {
      eraseAllData();
      sendHTML(client, "Semua data berhasil dihapus.");
    } 
    else if (request.indexOf("GET /delete?id=") >= 0) {
      int idIndex = request.indexOf("id=") + 3;
      String id = request.substring(idIndex, request.indexOf(" ", idIndex));
      deleteRowById(id);
      sendHTML(client, "Data berhasil dihapus.");
    } 
    else if (request.indexOf("GET /add?") >= 0) {
      int i1 = request.indexOf("id=") + 3;
      int i2 = request.indexOf("&nama=");
      int i3 = request.indexOf("&unit=");
      String id = request.substring(i1, i2);
      String nama = request.substring(i2 + 6, i3);
      String unit = request.substring(i3 + 6, request.indexOf(" ", i3));
      writeData(id, nama, unit);
      sendHTML(client, "Data berhasil ditambahkan.");
    } 
    else {
      sendHTML(client);
    }

    delay(1);
    client.stop();
    Serial.println("Client disconnected");
  }
}
