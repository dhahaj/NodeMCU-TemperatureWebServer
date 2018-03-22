/* Home Weather Station
   Connect to a WiFi network and provide a web server on it so show Temperature, Humidity, Dew Point, Pressure, and Uptime as well as a forecast widget from weatherforyou.com.
   Connect to http://ip:8890/update for webpage updater.
   History.html is hosted on another server.
   Webpage auto refreshes every 6 hours to update the forecast widget.  (websocket or ajax would be nice here as well)
   Forward ports 8888 - 8890 to this ip in your router to access remotely.
*/

#include "definitions.h"
#include <ESP8266HTTPUpdateServer.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
//#include <Adafruit_BME280.h>
#include <Wire.h>
#include <BME280I2C.h>
#include <FS.h>
#include <WebSocketsServer.h> // Version vom 20.05.2015 https://github.com/Links2004/arduinoWebSockets
#include <PrintEx.h>

StreamEx stream = Serial;
using namespace ios;

#define DBG_OUTPUT_PORT Serial

char *getipstr(void);
void setupAP(void);

const char* host = "ESP8266-BME280";
const char* update_path = "/update";
const char* update_username = "daniel";
const char* update_password = "jnco";
const char *ssid = "DETEX5GHz";
const char *password = "jnco5626";

const char thingSpeakAddress[] = "api.thingspeak.com";
String APIKey = "YATHN04SIJ51IXGW";	//enter your channel's Write API Key
const int updateThingSpeakInterval = 60000UL;		// 1 minute interval at which to update ThingSpeak

long lastConnectionTime = 0;
boolean lastConnected = false;

WiFiClient client;

//Adafruit_BME280 bmp;
BME280I2C bmp;

bool metric = false;
String temp_str, /*alt_str, */pres_str, hum_str;

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
        DBG_OUTPUT_PORT.print(F("handleFileUpload Name: "));
        DBG_OUTPUT_PORT.println(filename);
        fsUploadFile = SPIFFS.open(filename, "w");
        filename = String();
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        //DBG_OUTPUT_PORT.print("handleFileUpload Data: "); DBG_OUTPUT_PORT.println(upload.currentSize);
        if (fsUploadFile)
            fsUploadFile.write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
        if (fsUploadFile)
            fsUploadFile.close();
        DBG_OUTPUT_PORT.print(F("handleFileUpload Size: "));
        DBG_OUTPUT_PORT.println(upload.totalSize);
    }
}

void handleFileDelete() {
    if (server.args() == 0) return server.send(500, "text/plain", "BAD ARGS");
    String path = server.arg(0);
    DBG_OUTPUT_PORT.println(("handleFileDelete: " + path));
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
    sResponse += FPSTR(RESPONSE);
    server.send ( 200, "text/html", sResponse );
    stream << F("Client disonnected") << endl;
}

//------------------------------------------------------WEBPAGE HTTP HISTORY-------------------------------------------------
void http_history() {
    String sResponse;
    sResponse = FPSTR(HEAD_BEGIN);
    sResponse += FPSTR(HEAD_END);
    sResponse += FPSTR(HISTORY);
    server.send ( 200, "text/html", sResponse );
    stream << F("Client disonnected") << endl;
}

//--------------------------------------------------------Websocket Event----------------------------------------------------
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) {

    switch (type)
    {
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
            stream << F("webSocketEvent else\n");
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

void bmpSample() {
    float temp(NAN), alt(NAN), pres(NAN), hum(NAN);
    //    temp = (bmp.readTemperature() * 1.8 + 32.0),
    //    pres = ((bmp.readPressure() / 100.0F)),
    //    alt = (bmp.readAltitude(SEALEVELPRESSURE_HPA)),
    //    hum = bmp.readHumidity();

    BME280::TempUnit tempUnit(BME280::TempUnit_Celsius);
    BME280::PresUnit presUnit(BME280::PresUnit_Pa);

    bmp.read(pres, temp, hum, tempUnit, presUnit);

    temp_str = (temp),
    //    alt_str = (alt),
    pres_str = (pres),
    hum_str = (hum);

    temp_str = temp_str.substring(0, temp_str.length() - 1);
    //alt_str = alt_str.substring(0, alt_str.length() - 1); //, pres_str = pres_str.substring(0, pres_str.length() - 1);

    //    stream << F("Temperature: ") << temp_str << 'F' << endl << F("Altitude: ") << alt_str << 'm' << endl << F("Pressure: ") << pres_str << "hPa" << endl;
    //    stream << F("Humidity: ") << hum_str << '%' << endl;
}

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
            stream << F("Connecting to ThingSpeak...") << endl << endl;
    }
}

void thingSpeak() {

    if (client.available()) {
        char c = client.read();
        stream << (c);
    }

    // Disconnect from ThingSpeak
    if (!client.connected() && lastConnected) {
        stream << F("...disconnected") << endl << endl;
        client.stop();
    }

    // Update ThingSpeak
    if (!client.connected() && (millis() - lastConnectionTime > updateThingSpeakInterval))
        updateThingSpeak("field1=" + temp_str + "&field2=" + temp_str + "&field3=" + pres_str + "&field4=" + hum_str);
    lastConnected = client.connected();
}

void setup ( void ) {
    Serial.begin(9600);
    stream << F("Trying wifi config.") << endl;
    WiFi.begin(ssid, password);
    uint8_t i = 0;
    while ( WiFi.status() != WL_CONNECTED) {
        delay (1000);
        stream << '.';
        if(++i > 15) {
            stream << F("Could not connect to WiFi!.");
            while(1);
        }
    }

    stream << endl << F("Connected to ") << ssid << F(", IP address: ") << getipstr() << endl;
    // Serial.println(WiFi.localIP(), HEX);
    stream << "Own MAC: ";
    Serial.println(WiFi.macAddress());

    //START WEBSERVER UPGRADE
    MDNS.begin(host);

    httpUpdater.setup(&httpServer, update_path, update_username, update_password);
    httpServer.begin();

    MDNS.addService("http", "tcp", 8890);
    //Print the IP Address
    stream << "Use this URL to connect: " << "http://";
    Serial.print(WiFi.localIP());
    stream << getipstr();
    stream << ":8890" << "/update" << endl;

    // ------------------------------------------------------Arduino OTA------------------------------------------------------
    // Port defaults to 8266
    // ArduinoOTA.setPort(8266);

    // Hostname defaults to esp8266-[ChipID]
    // ArduinoOTA.setHostname("myesp8266");

    // No authentication by default
    // ArduinoOTA.setPassword((const char *)"123");

    //    ArduinoOTA.onStart([]() {
    //        stream << "Start" << endl;
    //    });
    //
    //    ArduinoOTA.onEnd([]() {
    //        stream << endl << "End" << endl;
    //    });

    //    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    //        stream.printf("Progress: %u%%\r", (progress / (total / 100)));
    //    });
    //
    //    ArduinoOTA.onError([](ota_error_t error) {
    //        stream.printf("Error[%u]: ", error);
    //        if (error == OTA_AUTH_ERROR) stream << "Auth Failed" << endl;
    //        else if (error == OTA_BEGIN_ERROR) stream << "Begin Failed" << endl;
    //        else if (error == OTA_CONNECT_ERROR) stream << "Connect Failed" << endl;
    //        else if (error == OTA_RECEIVE_ERROR) stream << "Receive Failed" << endl;
    //        else if (error == OTA_END_ERROR) stream << "End Failed" << endl;
    //    });
    //
    //    ArduinoOTA.begin();

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
    stream << F("HTTP server started") << endl;

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    stream << F("Socket server started") << endl;

    stream << F("BME280 test") << endl;
    Wire.begin();
    while(!bmp.begin())
    {
        Serial.println("Could not find BME280 sensor!");
        delay(1000);
    }

    //    if (!bmp.begin()) {
    //        stream << F("Could not find a valid BME sensor, check wiring!");
    //        while(1);
    //    }
}

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
        //        webSocket.broadcastTXT("3\:" + alt_str);
        webSocket.broadcastTXT("4\:" + pres_str);
        webSocket.broadcastTXT("5\:" + hum_str);
        time_poll = millis() + 1000;
    }
}

char *getipstr(void) {
    static char cOut[20];
    IPAddress ip = WiFi.localIP(); // the IP address of your esp
    sprintf(cOut, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]); //also pseudocode.
    return cOut;
}
