/* Home Weather Station
   Connect to a WiFi network and provide a web server on it so show Temperature, Pressure, and Uptime as well as
   a forecast widget from weatherforyou.com.
   Connect to http://ip:8890/update for webpage updater.
   Can be flashed OTA or http updater.
   Send all sensor readings to Thingspeak.com once every minute.
   History.html is hosted on another server.
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
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>


#define DHTTYPE DHT11
#define DHTPIN  2
DHT_Unified dht(DHTPIN, DHTTYPE);

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

// Thingspeak stuff
char thingSpeakAddress[] = "api.thingspeak.com";
String APIKey = "NLQLLP7VZ8JDHDA7";
// String APIKey = "EX2W4F4J2DBU9O90";              //enter your channel's Write API Key
const int updateThingSpeakInterval = 60 * 1000;  // 1 minute interval at which to update ThingSpeak

long lastConnectionTime = 0;
boolean lastConnected = false;
WiFiClient client;

//Adafruit_BMP085 bmp;
bool metric = false;
//String temp_str, alt_str, pres_str;
String temp_str, humidity_str;

ESP8266WebServer server (7777);
File fsUploadFile;
// HTTP Updater page
ESP8266WebServer httpServer(7780);
ESP8266HTTPUpdateServer httpUpdater;

WebSocketsServer webSocket = WebSocketsServer(7778);

uint32_t time_poll = 0;

//-------------------------------------------------HTTP START---------------------------------------------------------
const char HEAD_BEGIN[] PROGMEM = "<!DOCTYPE html>\r\n<html lang=\"en\">\r\n<head>\r\n<meta charset=\"utf-8\"/>\r\n<meta http-equiv=\"refresh\" content=\"21600\">\r\n";

const char WEBSOCKET_SCRIPT[] PROGMEM = "<script>\r\nvar connection = new WebSocket('ws://'+location.hostname+':7778/',['arduino']);\r\n"
                                        "connection.onmessage = function (e){ \r\n"
                                        "console.log('Server: ', e.data);\r\n"
                                        "if(e.data.slice(0,2)==\"1:\") document.getElementById('ActUptime').value = e.data.slice(2);\r\n"
                                        "if(e.data.slice(0,2)==\"2:\") document.getElementById('ActTemp').value = e.data.slice(2);\r\n"
                                        "if(e.data.slice(0,2)==\"3:\") document.getElementById('ActAlt').value = e.data.slice(2);\r\n"
                                        "if(e.data.slice(0,2)==\"4:\") document.getElementById('ActPres').value = e.data.slice(2);\r\n"
                                        "}\r\n"
                                        "connection.onclose = function(){\r\n"
                                        "console.log('closed!')\r\n check();\r\n}\r\n"
                                        "function check(){\r\n if(!connection || connection.readyState == 3){\r\n"
                                        "setInterval(check, 5000);\r\n"
                                        "}\r\n}\r\n</script>\r\n";
const char STYLE[] PROGMEM = "<style>\r\n"
                             "body { background-color: #000000; font-family: Arial, Helvetica, Sans-Serif; Color: #ffffff; margin: 3px;}\r\n"
                             "a { text-decoration: none; color: #d3d3d3; }\r\n"
                             "#if { width: 400px; overflow: auto; } \r\n</style>\r\n";
const char TITLE[] PROGMEM = "<title>[TITLE]</title>\r\n";
const char ROOT[] PROGMEM = "<body>\r\n<center>\r\n<table>\r\n<tbody>\r\n<tr>\r\n"
                            "<td colspan = \"3\" align=\"left\"><a href=\"history.html\" target=\"_new\">Temperature<br></a><font size=\"+4\"><output name=\"ActTemp\" id=\"ActTemp\"></output>&deg;F  </font></td>\r\n"
                            //                            "</tr>\r\n<tr>\r\n<td align=\"left\"><a href=\"history.html\" target=\"_new\">Altitude<br></a><font size=\"+2\"><output name=\"ActAlt\" id=\"ActAlt\"></output>m </font><p></td>\r\n"
                            "<td align=\"right\"><a href=\"history.html\" target=\"_new\">Humidity<br></a><font size=\"+4\"><output name=\"ActAlt\" id=\"ActAlt\"></output>%</font></td>\r\n<p>"
                            "</tr>\r\n<tr>\r\n<td colspan=\"3\" align=\"center\">Uptime: <output name=\"ActUptime\" id=\"ActUptime\"></output><p></td>\r\n</tr>\r\n"
                            "<tr width=\"250\"><td colspan=\"3\"><div class=\"if\">\r\n<a href=\"https://www.weatherforyou.com/weather/tx/new%20braunfels.html\" target=\"_new\">\r\n"
                            "<img src=\"https://www.weatherforyou.net/fcgi-bin/hw3/hw3.cgi?config=png&forecast=zone&alt=hwizone7day5&place=new%20braunfels&state=tx&country=us&hwvbg=black&hwvtc=white&hwvdisplay=&daysonly=2&maxdays=7\""
                            " width=\"600\" height=\"300\" border=\"0\" alt=\"new%20braunfels, tx, weather forecast\"></a>\r\n"
                            "</div></td>\r\n</tr>\r\n</tbody>\r\n</table>\r\n</body>\r\n</html>\r\n";
const char HEAD_END[] PROGMEM = "</head>\r\n";
const char HISTORY[] PROGMEM = "<style> body { padding: 4px; margin: 0; } button { color: green; }</style>\r\n"
                               "<body><center>\r\n"
                               "<br><button type=\"button\" onclick=\"javascript:window.close()\"><a> Close Window  </a></button><br><br>\r\n"
                               "<iframe width=\"450\" height=\"260\" style=\"border: 1px solid #cccccc;\" src=\"https://thingspeak.com/channels/297117/charts/1?bgcolor=%23000000&color=%23ffffff&days=1&dynamic=true&median=10&results=30&title=Temperature&type=line&xaxis=Date&yaxis=Temp%28F%29&yaxismax=100&yaxismin=50\"></iframe> &nbsp;\r\n"
                               "<iframe width=\"450\" height=\"260\" style=\"border: 1px solid #cccccc;\" src=\"https://thingspeak.com/channels/297117/charts/2?average=10&bgcolor=%23000000&color=%23ffffff&days=1&dynamic=true&results=30&title=Humidity&type=line&xaxis=Date&yaxis=Relative+Humidity&yaxismax=100&yaxismin=0\"></iframe><br>\r\n"
                               "</body>\r\n</html>\r\n";
//-----------------------------------------------------WEBPAGE HTTP ROOT---------------------------------------------------
void http_root() {
  String sResponse = "";
  sResponse += FPSTR(HEAD_BEGIN);
  sResponse += FPSTR(WEBSOCKET_SCRIPT);
  sResponse.replace("[IP]", getipstr());
  sResponse += FPSTR(TITLE);
  sResponse.replace("[TITLE]", "Home Weather Station");
  sResponse += FPSTR(STYLE);
  sResponse += FPSTR(HEAD_END);
  sResponse += FPSTR(ROOT);
  server.send ( 200, "text/html", sResponse );
  stream << "Client disonnected" << endl;
}
//------------------------------------------------------WEBPAGE HTTP HISTORY-------------------------------------------------
void http_history() {
  String sResponse =  "";
  sResponse += FPSTR(HEAD_BEGIN);
  sResponse += FPSTR(STYLE);
  sResponse += FPSTR(HEAD_END);
  sResponse += FPSTR(HISTORY);
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
      stream.printf("[%u] got Text: %s\n", num, payload);
      break;
    case WStype_BIN:
      stream.printf("[%u] got binary length: %u\n", num, lenght);
      hexdump(payload, lenght);
      break;
    default:
      stream << "webSocketEvent else" << endl;
  }

}

void handleNotFound() {
  String message = "";
  message += "File Not Found\n\nURI: ";
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
      stream_P(PSTR("Connecting to ThingSpeak...\r\n"));
  }
}

void thingSpeak() {
  if (client.available()) {
    char c = client.read();
    stream << (c);
  }
  // Disconnect from ThingSpeak
  if (!client.connected() && lastConnected) {
    stream_P(PSTR("...disconnected\r\n"));
    client.stop();
  }
  // Update ThingSpeak
  if (!client.connected() && (millis() - lastConnectionTime > updateThingSpeakInterval))
    updateThingSpeak("field1=" + temp_str + "&field2=" + humidity_str); // + "&field3=" + pres_str); // + "&field4=" + pres_str);
  lastConnected = client.connected();
}

void setup (void) {
  Serial.begin(9600);
  stream << "Trying wifi config" << endl;
  WiFi.begin(ssid, password);
  uint8_t i = 0;
  while ( WiFi.status() != WL_CONNECTED && (i++) < 10) {
    delay(500);
    stream << ".";
  }
  stream_P(PSTR("\r\nConnected to "));
  stream << ssid << endl;
  stream_P(PSTR("IP address: ")); // stream.println(WiFi.localIP(), HEX);
  stream << getipstr() << endl;
  stream_P(PSTR("Own MAC: "));
  stream.println(WiFi.macAddress());

  //START WEBSERVER UPGRADE
  MDNS.begin(host);

  httpUpdater.setup(&httpServer, update_path, update_username, update_password);
  httpServer.begin();

  MDNS.addService("http", "tcp", 8890);
  stream_P(PSTR("Use this URL to connect: http://"));
  stream << getipstr() << ":8890/update" << endl;

  // ------------------------------------------------------Arduino OTA------------------------------------------------------
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);
  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");
  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    stream_P(PSTR("Start\r\n"));
  });
  ArduinoOTA.onEnd([]() {
    stream_P(PSTR("\r\nEnd\r\n"));
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    stream.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    stream.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)          stream_P(PSTR("Auth Failed\r\n"));
    else if (error == OTA_BEGIN_ERROR)    stream_P(PSTR("Begin Failed\r\n"));
    else if (error == OTA_CONNECT_ERROR)  stream_P(PSTR("Connect Failed\r\n"));
    else if (error == OTA_RECEIVE_ERROR)  stream_P(PSTR("Receive Failed\r\n"));
    else if (error == OTA_END_ERROR)      stream_P(PSTR("End Failed\r\n"));
  });
  ArduinoOTA.begin();

  SPIFFS.begin();
  //SPIFFS.format(); // only uncomment if you want to format SPIFFS.  After flashing run once and then comment back.

  //-------------------------------------------- SERVER HANDLES -------------------------------------------------------
  server.on("/", http_root);
  server.on("/history.html", http_history);
  server.on("/history", http_history);
  //server.onNotFound ( handleNotFound );

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

  //called when the url is not defined here
  //use it to load content from SPIFFS
  server.onNotFound([]() {
    if (!handleFileRead(server.uri()))
      server.send(404, "text/plain", "FileNotFound");
  });

  //get heap status, analog input value and all GPIO statuses in one json call
  server.on("/all", HTTP_GET, []() {
    String json = "{";
    json += "\"heap\":" + String(ESP.getFreeHeap());
    json += ", \"analog\":" + String(analogRead(A0));
    json += ", \"gpio\":" + String((uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16)));
    json += "}";
    server.send(200, "text/json", json);
    json = String();
  });

  server.begin();
  stream_P(PSTR("HTTP server started\r\n"));

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  stream_P(PSTR("Socket server started\r\n"));

  dht.begin();
  sensor_t sensor;
  dht.temperature().getSensor(&sensor);
  dht.humidity().getSensor(&sensor);
}

void loop ( void ) {
  server.handleClient();
  ArduinoOTA.handle();
  httpServer.handleClient();
  webSocket.loop();
  thingSpeak();

  if (time_poll <= millis()) {
    dhtSample();
    int sec = millis() / 1000,
        min = sec / 60,
        hr = min / 60,
        day = hr / 24;
    char Uptime[24];
    sprintf(Uptime, "1:%02dd:%02dh:%02dm:%02ds", day, hr % 24, min % 60, sec % 60);
    webSocket.broadcastTXT(Uptime);
    webSocket.broadcastTXT("2\:" + temp_str);
    webSocket.broadcastTXT("3\:" + humidity_str);
    time_poll = millis() + 2000;
  }
}

String formatBytes(size_t bytes) {
  if (bytes < 1024) {
    return String(bytes) + "B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + "KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  } else {
    return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
  }
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

char *getipstr(void) {
  static char cOut[20];
  IPAddress ip = WiFi.localIP(); // the IP address of your esp
  sprintf(cOut, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]); //also pseudocode.
  return cOut;
}

void stream_P(const char *s) {
  stream.printf("%p", s);
}

void dhtSample() {
  static String t = "", h = "";

  sensors_event_t event;
  dht.temperature().getEvent(&event);
  if (isnan(event.temperature)) {
    Serial.println("Error reading temperature!");
    temp_str = t;
  }
  else {
    temp_str = String(event.temperature * 1.8 + 32.0);
    t = temp_str;
    stream_P(PSTR("Temperature: "));
    stream << (event.temperature * 1.8 + 32.0) << "ºF" << endl;
  }

  // Get humidity event and print its value.
  dht.humidity().getEvent(&event);
  if (isnan(event.relative_humidity)) {
    stream_P(PSTR("Error reading humidity!"));
    humidity_str = h;
  }
  else {
    humidity_str = String(event.relative_humidity);
    h = humidity_str;
    stream_P(PSTR("Humidity: "));
    stream << event.relative_humidity << "%" << endl;
  }
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
  if (!server.hasArg("dir")) {
    server.send(500, "text/plain", "BAD ARGS");
    return;
  }

  String path = server.arg("dir");
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




