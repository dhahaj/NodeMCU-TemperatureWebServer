/* Home Weather Station
   Connect to a WiFi network and provide a web server on it so show Temperature, Humidity, Dew Point, Pressure, and Uptime as well as a forecast widget from weatherforyou.com.
   Connect to http://ip:8890/update for webpage updater.
   History.html is hosted on another server.
   Webpage auto refreshes every 6 hours to update the forecast widget.  (websocket or ajax would be nice here as well)
   Forward ports 8888 - 8890 to this ip in your router to access remotely.
*/

#include <ESP8266HTTPUpdateServer.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Adafruit_BMP085.h>
#include <FS.h>
#include <WebSocketsServer.h> // Version vom 20.05.2015 https://github.com/Links2004/arduinoWebSockets
#include <PrintEx.h>

StreamEx stream = Serial;
using namespace ios;
#define DBG_OUTPUT_PORT Serial

char *getipstr(void);
void setupAP(void);

const char* host = "esp8266-weather";
const char* update_path = "/update";
const char* update_username = "daniel";
const char* update_password = "jnco";
const char *ssid = "fkyall";
const char *password = "%%jnco5626%%";

const char thingSpeakAddress[] = "api.thingspeak.com";
String APIKey = "EX2W4F4J2DBU9O90";	//enter your channel's Write API Key
const int updateThingSpeakInterval = 60000UL;		// 1 minute interval at which to update ThingSpeak

long lastConnectionTime = 0;
boolean lastConnected = false;
WiFiClient client;

Adafruit_BMP085 bmp;
bool metric = false;
String temp_str, alt_str, pres_str;

ESP8266WebServer server(8888);

// HTTP Updater page
ESP8266WebServer httpServer(8890);
ESP8266HTTPUpdateServer httpUpdater;
WebSocketsServer webSocket = WebSocketsServer(8889);
uint32_t time_poll = 0;

//holds the current upload
File fsUploadFile;

//format bytes
String formatBytes(size_t bytes) {
  if (bytes < 1024)
    return String(bytes) + "B";
  else if (bytes < (1024 * 1024))
    return String(bytes / 1024.0) + "KB";
  else if (bytes < (1024 * 1024 * 1024))
    return String(bytes / 1024.0 / 1024.0) + "MB";
  else
    return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
}

String getContentType(String filename) {
  if (server.hasArg("download")) return "application/octet-stream";
  else if (filename.endsWith(".htm")) return "text/html";
  else if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".png")) return "image/png";
  else if (filename.endsWith(".gif")) return "image/gif";
  else if (filename.endsWith(".jpg")) return "image/jpeg";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".xml")) return "text/xml";
  else if (filename.endsWith(".pdf")) return "application/x-pdf";
  else if (filename.endsWith(".zip")) return "application/x-zip";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path) {
  DBG_OUTPUT_PORT.println("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.htm";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
    if (SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void handleFileUpload() {
  if (server.uri() != "/edit") return;
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) filename = "/" + filename;
    DBG_OUTPUT_PORT.print("handleFileUpload Name: "); DBG_OUTPUT_PORT.println(filename);
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    //DBG_OUTPUT_PORT.print("handleFileUpload Data: "); DBG_OUTPUT_PORT.println(upload.currentSize);
    if (fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile)
      fsUploadFile.close();
    DBG_OUTPUT_PORT.print("handleFileUpload Size: "); DBG_OUTPUT_PORT.println(upload.totalSize);
  }
}

void handleFileDelete() {
  if (server.args() == 0) return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  DBG_OUTPUT_PORT.println("handleFileDelete: " + path);
  if (path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if (!SPIFFS.exists(path))
    return server.send(404, "text/plain", "FileNotFound");
  SPIFFS.remove(path);
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileCreate() {
  if (server.args() == 0)
    return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  DBG_OUTPUT_PORT.println("handleFileCreate: " + path);
  if (path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if (SPIFFS.exists(path))
    return server.send(500, "text/plain", "FILE EXISTS");
  File file = SPIFFS.open(path, "w");
  if (file)
    file.close();
  else
    return server.send(500, "text/plain", "CREATE FAILED");
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileList() {
  String path;
  if (!server.hasArg("dir")) {
    path = "/";
    //    server.send(500, "text/plain", "BAD ARGS");
    //    return;
  } else {
    path = server.arg("dir");
  }
  // String path = server.arg("dir");
  DBG_OUTPUT_PORT.println("handleFileList: " + path);
  Dir dir = SPIFFS.openDir(path);
  path = String();

  String output = "[";
  while (dir.next()) {
    File entry = dir.openFile("r");
    if (output != "[") output += ',';
    bool isDir = false;
    output += "{\"type\":\"";
    output += (isDir) ? "dir" : "file";
    output += "\",\"name\":\"";
    output += String(entry.name()).substring(1);
    output += "\"}";
    entry.close();
  }
  output += "]";
  server.send(200, "text/json", output);
}


//-------------------------------------------------HTTP START---------------------------------------------------------
const char HEAD_BEGIN[] PROGMEM = "<!DOCTYPE html>\r\n<html lang=\"en\">\r\n<head>\r\n<meta charset=\"utf-8\"/>\r\n<meta http-equiv=\"refresh\" content=\"21600\">\r\n";

const char WEBSOCKET_SCRIPT[] PROGMEM = "<script>\r\nvar connection = new WebSocket('ws://'+location.hostname+':8889/', ['arduino']);\r\n"
                                        "connection.onmessage = function(e){\r\n"
                                        " console.log('Server: ', e.data);\r\n"
                                        " if(e.data.slice(0,2)==\"1:\") document.getElementById('ActUptime').value = e.data.slice(2);\r\n"
                                        " if(e.data.slice(0,2)==\"2:\") document.getElementById('ActTemp').value = e.data.slice(2);\r\n"
                                        " if(e.data.slice(0,2)==\"3:\") document.getElementById('ActAlt').value = e.data.slice(2);\r\n"
                                        " if(e.data.slice(0,2)==\"4:\") document.getElementById('ActPres').value = e.data.slice(2);\r\n }\r\n"
                                        "connection.onclose = function(){\r\n"
                                        " console.log('closed!');\r\n"
                                        " check();\r\n}\r\n"
                                        "function check(){\r\n"
                                        " if(!connection || connection.readyState==3)\r\n"
                                        " setInterval(check,5000);\r\n}\r\n"
                                        "</script>\r\n";
const char STYLE[] PROGMEM = "<style>\r\nbody { background-color: #000000; font-family: Arial, Helvetica, Sans-Serif; Color: #ffffff; } "
                             "a { text-decoration: none; color: #d3d3d3; }\r\n</style>\r\n";
const char TITLE[] PROGMEM = "<title>[TITLE]</title>\r\n";
const char HEAD_END[] PROGMEM = "</head>\r\n";

//-----------------------------------------------------WEBPAGE HTTP ROOT---------------------------------------------------
void http_root() {
  String sResponse;
  sResponse = FPSTR(HEAD_BEGIN);
  sResponse += FPSTR(WEBSOCKET_SCRIPT);
  sResponse.replace("[IP]", getipstr());
  sResponse += FPSTR(TITLE);
  sResponse.replace("[TITLE]", "Home Weather Station");
  sResponse += FPSTR(STYLE);
  sResponse += FPSTR(HEAD_END);
  sResponse += "<body>\r\n"
               "<center>\r\n"
               "<table><tbody><tr>\r\n"
               "<td colspan = \"3\" align=\"center\"><a href=\"history.html\" target=\"_new\">Temperature<br></a><font size=\"+4\"><output name=\"ActTemp\" id=\"ActTemp\"></output> &deg;F</font><p></td>\r\n"
               "</tr><tr><td align=\"left\"><a href=\"history.html\" target=\"_new\">Altitude<br></a><font size=\"+2\"><output name=\"ActAlt\" id=\"ActAlt\"></output> meters</font><p></td>\r\n"
               "<td align=\"right\"><a href=\"history.html\" target=\"_new\">Pressure<br></a><font size=\"+2\">"
               "<output name=\"ActPres\" id=\"ActPres\"></output> pascal</font><p></td>\r\n"
               "</tr>"
               "<tr><td colspan=\"3\" align=\"center\">Uptime: <output name=\"ActUptime\" id=\"ActUptime\"></output><p></td>\r\n"
               "</tr>"
               "<tr width=\"250\"><td colspan=\"3\"><div style='width: 300px; overflow: auto;'>\r\n"
               "<a href=\"https://www.weatherforyou.com/weather/tx/new%20braunfels.html\" target=\"_new\">\r\n"
               "<img src=\"https://www.weatherforyou.net/fcgi-bin/hw3/hw3.cgi?config=png&forecast=zone&alt=hwizone7day5&place=new%20braunfels&state=tx&country=us&hwvbg=black&hwvtc=white&hwvdisplay=&daysonly=2&maxdays=7\" width=\"500\" height=\"200\" border=\"0\" alt=\"new%20braunfels, tx, weather forecast\"></a>\r\n"
               "</div></td>"
               "</tr>\r\n"
               "</tbody>"
               "</table>\r\n"
               "</body>\r\n"
               "</html>\r\n";
  server.send ( 200, "text/html", sResponse );
  stream << "Client disonnected" << endl;;
}

//------------------------------------------------------WEBPAGE HTTP HISTORY-------------------------------------------------
void http_history() {
  String sResponse;
  sResponse = FPSTR(HEAD_BEGIN);
  sResponse += FPSTR(HEAD_END);
  sResponse += "<body>"
               "<center>\r\n"
               "<button type=\"button\" onclick=\"javascript:window.close()\">  Close Window  </button><br><br>\r\n"
               "<iframe width=\"500\" height=\"300\" style=\"border: 1px solid #cccccc;\""
               " src=\"https://thingspeak.com/channels/126450/charts/1?bgcolor=%23ffffff&color=%23d62020&dynamic=true&results=40&title=Temperature&type=line\">"
               "</iframe><br>\r\n"
               "<iframe width=\"500\" height=\"300\" style=\"border: 1px solid #cccccc;\""
               " src=\"https://thingspeak.com/channels/126450/charts/2?bgcolor=%23ffffff&color=%23d62020&dynamic=true&results=40&title=Altitude&type=line\">"
               "</iframe><br>\r\n"
               "<iframe width=\"450\" height=\"260\" style=\"border: 1px solid #cccccc;\""
               " src=\"https://thingspeak.com/channels/126450/charts/3?bgcolor=%23ffffff&color=%23d62020&dynamic=true&results=40&title=Pressure&type=line\">"
               "</iframe>\r\n"
               "<iframe width=\"450\" height=\"260\" style=\"border: 1px solid #cccccc;\" src=\"https://thingspeak.com/channels/126450/maps/channel_show\">"
               "</iframe>\r\n"
               "</body>\r\n"
               "</html>\r\n";
  server.send ( 200, "text/html", sResponse );
  stream << "Client disonnected" << endl;
}

//--------------------------------------------------------Websocket Event----------------------------------------------------
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) {

  switch (type) {
    case WStype_DISCONNECTED:
      stream.printf("[%u] Disconnected!\n", num);
      webSocket.disconnect(num);
      break;

    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        stream.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        //ForceSendValues = 1;
        webSocket.sendTXT(num, "Connected");
      }
      break;

    case WStype_TEXT:
      Serial.printf("[%u] got Text: %s\n", num, payload);
      break;

    case WStype_BIN:
      Serial.printf("[%u] got binary length: %u\n", num, lenght);
      hexdump(payload, lenght);
      break;

    default:
      stream << "webSocketEvent else\n";
  }
}

void handleNotFound() {
  String message = "File Not Found\n\nURI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += ( server.method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for ( uint8_t i = 0; i < server.args(); i++ )
    message += " " + server.argName ( i ) + ": " + server.arg ( i ) + "\n";
  server.send( 404, "text/plain", message );
}

//--------------------------------------------------------BMP280-------------------------------------------------------
void bmpSample() {
  float temp(NAN), alt(NAN), pres(NAN);
  temp = (bmp.readTemperature() * 1.8 + 32.0),  pres = bmp.readPressure(), alt = bmp.readAltitude();
  temp_str = (temp), alt_str = (alt), pres_str = (pres);
  temp_str = temp_str.substring(0, temp_str.length() - 1), alt_str = alt_str.substring(0, alt_str.length() - 1), pres_str = pres_str.substring(0, pres_str.length() - 1);
  //stream << "Temperature: " << temp_str << "F" << endl << "Altitude: " << alt_str << "m" << endl << "Pressure: " << pres_str << "pas" << endl;
}

//-------------------------------------------------------Thingspeak----------------------------------------------
void updateThingSpeak(String tsData) {
  if (client.connect(thingSpeakAddress, 80)) {
    client.print("POST /update HTTP/1.1\n");
    client.print("Host: api.thingspeak.com\n");
    client.print("Connection: close\n");
    client.print("X-THINGSPEAKAPIKEY: " + APIKey + "\n");
    client.print("Content-Type: application/x-www-form-urlencoded\n");
    client.print("Content-Length: ");
    client.print(tsData.length());
    client.print("\n\n");
    client.print(tsData);
    lastConnectionTime = millis();

    if (client.connected())
      stream << "Connecting to ThingSpeak..." << endl << endl;
  }
}

void thingSpeak() {

  if (client.available()) {
    char c = client.read();
    stream << (c);
  }

  // Disconnect from ThingSpeak
  if (!client.connected() && lastConnected) {
    stream << "...disconnected" << endl << endl;
    client.stop();
  }

  // Update ThingSpeak
  if (!client.connected() && (millis() - lastConnectionTime > updateThingSpeakInterval))
    updateThingSpeak("field1=" + temp_str + "&field2=" + alt_str + "&field3=" + pres_str); // + "&field4=" + pres_str);
  lastConnected = client.connected();
}

void setup ( void ) {
  //  Serial.begin(9600);
  //  stream << "Trying wifi config" << endl;
  WiFi.begin(ssid, password);
  uint8_t i = 0;
  while ( WiFi.status() != WL_CONNECTED && i < 10) {
    delay (500);
    //    stream << ".";
    i++;
  }
  //  stream << endl << "Connected to " << ssid << endl;
  // Serial.println(ssid);
  //  stream << "IP address: " << getipstr() << endl;
  //Serial.println(WiFi.localIP(), HEX);
  //  stream << "Own MAC: "; Serial.println(WiFi.macAddress());

  //START WEBSERVER UPGRADE
  MDNS.begin(host);

  httpUpdater.setup(&httpServer, update_path, update_username, update_password);
  httpServer.begin();

  MDNS.addService("http", "tcp", 8890);
  //Print the IP Address
  //  stream << "Use this URL to connect: " << "http://";
  // Serial.print(WiFi.localIP());
  //  stream << getipstr();
  //  stream << ":8890" << "/update" << endl;

  // ------------------------------------------------------Arduino OTA------------------------------------------------------
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    //    stream << "Start" << endl;
  });

  ArduinoOTA.onEnd([]() {
    //    stream << endl << "End" << endl;
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    //    stream.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    //    stream.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) stream << "Auth Failed" << endl;
    else if (error == OTA_BEGIN_ERROR) stream << "Begin Failed" << endl;
    else if (error == OTA_CONNECT_ERROR) stream << "Connect Failed" << endl;
    else if (error == OTA_RECEIVE_ERROR) stream << "Receive Failed" << endl;
    else if (error == OTA_END_ERROR) stream << "End Failed" << endl;
  });

  ArduinoOTA.begin();

  SPIFFS.begin();
  //SPIFFS.format(); // only uncomment if you want to format SPIFFS.  After flashing run once and then comment back.

  //-------------------------------------------- SERVER HANDLES -------------------------------------------------------
  //SERVER INIT
  //list directory
  server.on("/list", HTTP_GET, handleFileList);

  //load editor
  server.on("/edit", HTTP_GET, []() {
    if (!handleFileRead("/edit.htm")) server.send(404, "text/plain", "FileNotFound");
  });

  //create file
  server.on("/edit", HTTP_PUT, handleFileCreate);

  //delete file
  server.on("/edit", HTTP_DELETE, handleFileDelete);

  //first callback is called after the request has ended with all parsed arguments
  //second callback handles file uploads at that location
  server.on("/edit", HTTP_POST, []() {
    server.send(200, "text/plain", "");
  }, handleFileUpload);

  //  server.on("/", http_root);

  server.on("/", HTTP_GET, []() {
    if (!handleFileRead("/index.htm")) server.send(404, "text/plain", "FileNotFound");
  });

  // server.on("/history.html", http_history);

  server.on("/history.html", HTTP_GET, []() {
    if (!handleFileRead("/history.html")) server.send(404, "text/plain", "FileNotFound");
  });

  server.onNotFound([]() {
    if (!handleFileRead(server.uri()))
      server.send(404, "text/plain", "FileNotFound");
  });
  server.begin();

  //  stream << "HTTP server started" << endl;

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  //  stream << "Socket server started" << endl;

  //  stream << "BMP085 test" << endl;

  if (!bmp.begin()) {
    //    stream << "Could not find a valid BMP sensor, check wiring!" << endl;
    delay(1000);
  }
}

//------------------------------------------------------- LOOP -------------------------------------------------------
void loop ( void ) {
  server.handleClient();
  ArduinoOTA.handle();
  httpServer.handleClient();
  webSocket.loop();
  thingSpeak();
  bmpSample();

  if (time_poll <= millis()) {
    int sec = millis() / 1000,
        min = sec / 60,
        hr = min / 60,
        day = hr / 24;
    char Uptime[24];
    sprintf(Uptime, "1:%02dd:%02dh:%02dm:%02ds", day, hr % 24, min % 60, sec % 60);
    webSocket.broadcastTXT(Uptime);
    webSocket.broadcastTXT("2\:" + temp_str);
    webSocket.broadcastTXT("3\:" + alt_str);
    webSocket.broadcastTXT("4\:" + pres_str);
    //    webSocket.broadcastTXT("5\\:" + pres_str);
    time_poll = millis() + 1000;
  }
}

char *getipstr(void) {
  static char cOut[20];
  IPAddress ip = WiFi.localIP();                  // the IP address of your esp
  sprintf(cOut, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]); //also pseudocode.
  return cOut;
}
