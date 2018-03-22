
#ifndef _DEFINITIONS_H_
#define _DEFINITIONS_H_

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <PrintEx.h>

StreamEx stream = Serial;
using namespace ios;

ESP8266WebServer server(8888);

char* getipstr(void) {
  static char cOut[20];
  IPAddress ip = WiFi.localIP(); // the IP address of your esp
  sprintf(cOut, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]); //also pseudocode.
  return cOut;
}

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

const char HEAD_BEGIN[] PROGMEM = "<!DOCTYPE html>\r\n<html lang=\"en\">\r\n<head>\r\n<meta charset=\"utf-8\"/>\r\n<meta http-equiv=\"refresh\" content=\"21600\">\r\n";

const char RESPONSE[] PROGMEM = "<body>\r\n<center>\r\n<table><tbody>"
                                "<tr>\r\n"
                                "<td colspan = \"3\" align=\"center\"><a href=\"history.html\" target=\"_new\">Temperature<br></a><font size=\"+4\">"
                                "<output name=\"ActTemp\" id=\"ActTemp\"></output> &deg;F</font><p></td>\r\n</tr><tr><td align=\"left\"><a href=\"history.html\" target=\"_new\">Altitude<br></a><font size=\"+2\">"
                                "<output name=\"ActAlt\" id=\"ActAlt\"></output> meters</font><p></td>\r\n<td align=\"right\"><a href=\"history.html\" target=\"_new\">Pressure<br></a><font size=\"+2\">"
                                "<output name=\"ActPres\" id=\"ActPres\"></output> pascal</font><p></td>\r\n</tr>"
                                "<output name=\"ActHum\" id=\"ActHum\"></output> percent</font><p></td>\r\n</tr>"
                                //"<tr><td colspan=\"3\" align=\"center\">Uptime: <output name=\"ActUptime\" id=\"ActUptime\"></output><p></td>\r\n</tr>"
                                "<tr width=\"250\"><td colspan=\"3\"><div style='width: 300px; overflow: auto;'>\r\n"
                                "<a href=\"https://www.weatherforyou.com/weather/tx/new%20braunfels.html\" target=\"_new\">\r\n"
                                "<img src=\"https://www.weatherforyou.net/fcgi-bin/hw3/hw3.cgi?config=png&forecast=zone&alt=hwizone7day5&place=new%20braunfels&state=tx&country=us&hwvbg=black&hwvtc=white&hwvdisplay=&daysonly=2&maxdays=7\" width=\"500\" height=\"200\" border=\"0\" alt=\"new%20braunfels, tx, weather forecast\"></a>\r\n"
                                "</div></td></tr>\r\n</tbody></table>\r\n</body>\r\n</html>\r\n";

const char HISTORY[] PROGMEM = "<body>"
                               "<center>\r\n"
                               "<button type=\"button\" onclick=\"javascript:window.close()\">  Close Window  </button><br><br>\r\n"
                               "<iframe width=\"500\" height=\"300\" style=\"border: 1px solid #cccccc;\""
                               " src=\"https://thingspeak.com/channels/455374/charts/1?bgcolor=%23ffffff&color=%23d62020&dynamic=true&results=40&title=Temperature&type=line\">"
                               "</iframe><br>\r\n"
                               "<iframe width=\"500\" height=\"300\" style=\"border: 1px solid #cccccc;\""
                               " src=\"https://thingspeak.com/channels/455374/charts/2?bgcolor=%23ffffff&color=%23d62020&dynamic=true&results=40&title=Altitude&type=line\">"
                               "</iframe><br>\r\n"
                               "<iframe width=\"450\" height=\"260\" style=\"border: 1px solid #cccccc;\""
                               " src=\"https://thingspeak.com/channels/455374/charts/3?bgcolor=%23ffffff&color=%23d62020&dynamic=true&results=40&title=Pressure&type=line\">"
                               "</iframe>\r\n"
                               "<iframe width=\"450\" height=\"260\" style=\"border: 1px solid #cccccc;\""
                               " src=\"https://thingspeak.com/channels/455374/charts/4?bgcolor=%23ffffff&color=%23d62020&dynamic=true&results=40&title=Humidity&type=line\">"
                               "</iframe>\r\n"
                               // "<iframe width=\"450\" height=\"260\" style=\"border: 1px solid #cccccc;\" "
                               // "src=\"https://thingspeak.com/channels/455374/maps/channel_show\">"
                               // "</iframe>\r\n"
                               "</body>\r\n</html>\r\n";


//-----------------------------------------------------WEBPAGE HTTP ROOT---------------------------------------------------
void http_root(void)
{
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
void http_history(void)
{
  String sResponse;
  sResponse = FPSTR(HEAD_BEGIN);
  sResponse += FPSTR(HEAD_END);
  sResponse += FPSTR(HISTORY);
  server.send ( 200, "text/html", sResponse );
  stream << F("Client disonnected") << endl;
}

#endif
