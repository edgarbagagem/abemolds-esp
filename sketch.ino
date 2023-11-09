#include  <ESP8266WiFi.h>
#include  <ESP8266WebServer.h>

#include  "secrets.h"

#define HTTP_REST_PORT 80
ESP8266WebServer server(HTTP_REST_PORT);

int temps[] = {240, 250, 260, 200, 245, 300, 270, 220, 280, 255};
int arrSize = sizeof(temps)/sizeof(temps[0]);

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600); // Added a semicolon to end the statement

  //Wifi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
      Serial.println("Connecting...");
  }
  Serial.print("Connected to WiFi, Local IP address ");
  Serial.println(WiFi.localIP());

  //API
  restServerRouting();
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();

}


void getTemperature(){
  server.send(200, "text/json", String(temps[0]));
}

// Manage not found URL
void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}


void restServerRouting(){
      server.on("/", HTTP_GET, []() {
        server.send(200, F("text/html"),
            F("Welcome to the REST Web Server"));
    });
    server.on(F("/temperature"), HTTP_GET, getTemperature);
}