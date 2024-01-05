#include  <ESP8266WiFi.h>
#include  <ESP8266WebServer.h> 
#include <ArduinoJson.h>     // Need To Install
#include <Firebase_ESP_Client.h>
//Provide the token generation process info.
#include "addons/TokenHelper.h"
//Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"
#include  "secrets.h"
#include <TimeLib.h>
#include <WiFiUdp.h>

#define HTTP_REST_PORT 80

//https://github.com/Rupakpoddar/ESP8266Firebase/blob/master/examples/FirebaseJsonDemo/FirebaseJsonDemo.ino
#define FIREBASE_URL "https://abemolds-60b85-default-rtdb.europe-west1.firebasedatabase.app/"
// Insert Firebase project API Key
#define API_KEY "AIzaSyCozkZqMMRv1TooPSXfurG2OOEOJeIf5ik"
String mold_id = "1000";

//Define Firebase Data object
FirebaseData fbdo;

FirebaseAuth auth;
FirebaseConfig config;

ESP8266WebServer server(HTTP_REST_PORT);

const int timeZone = 0;
static const char ntpServerName[] = "pt.pool.ntp.org";
String curDateStr = "";
int today = 0;
int prevDay = -1;

WiFiUDP Udp;
unsigned int localPort = 8888;  // local port to listen for UDP packets

int maxCavityTemp;
int maxCoolingTemp;
int minCoolingTemp;
int maxCoolingMillis;
int minCoolingMillis;
int maxPlasticTemp;
int minPlasticTemp;
int maxInjectionFlow;
int minInjectionFlow;
int maxFillPressure;
int minFillPressure;
int maxFillMillis;
int minFillMillis;
int maxPackPressure;
int minPackPressure;
int maxPackMillis;
int minPackMillis;
int maxHoldPressure;
int minHoldPressure;

//int arrSize = sizeof(temps)/sizeof(temps[0]);
double lastCavityTemp = 30;
double lastAccelerometer = 0;
double lastFlow = 0;
double lastPressure = 0;

double curCavityTemp = 30;
double curPlasticTemp = 180;
double curAccelerometer = 0;
double curFlow = 0;
double curPressure = 0;
bool correctParameters = true;

bool acceptProd = true;
bool overrideUser = false;
unsigned int stage = 4;
//Available stages: 0 = "clamping", 1 = "injecting", 2 = "cooling", 3 = "ejecting"

//Demo variables
unsigned long demoStageStartMillis = 0;
unsigned int demoStage = 0;
/*double lastCavityTempDemo = 30;
double lastAccelerometerDemo = 0;
double lastFlowDemo = 0;
double lastPressureDemo = 0;*/

unsigned long stageStartMillis = 0;
unsigned long sendDataPrevMillis = 0;
bool signupOK = false;

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
  Serial.println("Starting UDP");
  Udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(Udp.localPort());
  Serial.println("waiting for sync");
  setSyncProvider(getNtpTime);
  setSyncInterval(300);

  /* Assign the api key (required) */
  config.api_key = API_KEY;

  /* Assign the RTDB URL (required) */
  config.database_url = FIREBASE_URL;

  /* Sign up */
  if (Firebase.signUp(&config, &auth, "", "")){
    Serial.println("ok");
    signupOK = true;
  }
  else{
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
  }

  /* Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  //Get manufacturing parameters
  getManufacturingParameters();

  demoStageStartMillis = millis();

  updateCorrectParameters();

  getDayStr();
}

void loop() {
  if (Firebase.ready() && signupOK) {
    demoMode();
    manageProduction();
  }
}


void demoMode() {
  //determine current stage's duration
  unsigned long demoStageMillis = millis() - demoStageStartMillis;

  //Serial.println("DemoStageSecs=" + demoStageMillis);

  double tempAux;

  switch(demoStage) {
    //Clamping
    case 0:
      Serial.println("Currently clamping");
      //Average duration is ~20s
      if (demoStageMillis > 10000) {
        curAccelerometer = 0;
        demoStage = 1;
        curFlow = (double(maxInjectionFlow) + double(minInjectionFlow)) / 2;
        curPressure = (double(maxFillPressure) + double(minFillPressure)) / 2;
        curPlasticTemp = (double(maxPlasticTemp) + double(minPlasticTemp)) / 2;
        demoStageStartMillis = millis();
        return;
      }

      //Temperature, Flow and Pressure don't change
      if (demoStageMillis < 5000) {
        curAccelerometer -= 0.05;
      }
      else if (curAccelerometer < 0) {
        curAccelerometer += 0.05;
      }

      break;
    case 1:
      Serial.println("Currently injecting - filling");

      if (demoStageMillis > minFillMillis) {
        curFlow = 0;
        demoStage = 2;
        demoStageStartMillis = millis();
        return;
      }

      curFlow *= double(random(996, 1004)) / 1000;
      curPlasticTemp *= double(random(998, 1002)) / 1000;

      //Filling mold
      if (demoStageMillis < minFillMillis - 5000) {
        tempAux = curCavityTemp * double(random(110, 120)) / 100;

        if (tempAux < curPlasticTemp) {
          curCavityTemp = tempAux;
        }
        else {
          curCavityTemp *= double(random(98, 100)) / 100;
        }

        curPressure *= double(random(996, 1004)) / 1000;
      }
      //Reduce pressure for packing
      //demoStageMillis >= minFillMillis - 5000 && demoStageMillis < minFillMillis &&s 
      else if (demoStageMillis >= minFillMillis - 5000 && demoStageMillis < minFillMillis && curPressure > maxPackPressure) {
        Serial.println("Reducing pressure for packing");
        tempAux = curCavityTemp * double(random(101, 105)) / 100;
        if (tempAux < curPlasticTemp) {
          curCavityTemp = tempAux;
        }
        else {
          curCavityTemp *= double(random(98, 100)) / 100;
        }
        double pressureAux = curPressure *= double(random(60, 70)) / 100;
        if (pressureAux < maxPackPressure && pressureAux > minPackPressure) {
          curPressure = pressureAux;
        }
        else if (pressureAux < minPackPressure) {
          curPressure *= double(random(85, 90));
        }
      }

      break;
    case 2:
      Serial.println("Currently injecting - packing");
      if (demoStageMillis > maxPackMillis - 2000) {
        demoStage = 3;
        demoStageStartMillis = millis();
      }

      tempAux = curCavityTemp * double(random(101, 104)) / 100;
      if (tempAux < curPlasticTemp) {
        curCavityTemp = tempAux;
      }
      else {
        curCavityTemp *= double(random(98, 100)) / 100;
      }
      
      if (curPressure > maxPackPressure) {
        curPressure *= double(random(96, 99)) / 100;
      }
      else {
        curPressure *= double(random(98, 102)) / 100;
      }
      
      break;
    case 3:
      Serial.println("Currently cooling");

      if (curCavityTemp < maxCoolingTemp) {
        demoStage = 4;
        demoStageStartMillis = millis();
        return;
      }

      curCavityTemp *= double(random(90, 98)) / 100;
      curPressure *= double(random(99, 102)) / 100;

      break;
    case 4:
      Serial.println("Currently ejecting");

      if (demoStageMillis > 10000) {
        curAccelerometer = 0;
        demoStage = 0;
        demoStageStartMillis = millis();
        return;
      }
      curPressure = 0;
      if (demoStageMillis < 5000) {
        curAccelerometer += 0.05;
      }
      else if (curAccelerometer < 0) {
        curAccelerometer -= 0.05;
      }
      break;
  }
}

void manageProduction() {
  unsigned long stageMillis = millis() - stageStartMillis;
  //send current data to database
  updateCavityTemperature();
  updatePlasticTemp();
  updatePressure();
  updateFlow();

  /*Serial.println("-------------------------------");
  Serial.print("StageMillis = ");
  Serial.println(stageMillis);
  Serial.print("CurAccelerometer = ");
  Serial.println(curAccelerometer);
  Serial.print("CurFlow = ");
  Serial.println(curFlow);
  Serial.print("CurCavityTemp = ");
  Serial.println(curCavityTemp);
  Serial.print("CurPlasticTemp = ");
  Serial.println(curPlasticTemp);
  Serial.print("CurPressure = ");
  Serial.println(curPressure);
  Serial.print("OverrideUser = ");
  Serial.println(overrideUser);*/

  //TODO get time for each stage - needs new variables and updating db

  //if mold is opening
  if (curAccelerometer > 0 && stage == 3) {
    //set stage to 'ejecting'
    stage = 4;
    updateStage();
    stageStartMillis = millis();
  }
  //or if mold is clamping
  else if (curAccelerometer < 0 && stage == 4) {
    //set stage to 'clamping'
    stage = 0;
    updateStage();
    stageStartMillis = millis();

    //increment total parts produced IF correctParameters is true
    if (correctParameters) {
      incrementPartsProduced();
    }

    //restart correctParameters boolean
    correctParameters = true;
    updateCorrectParameters();
  }

  //update last acceleration
  lastAccelerometer = curAccelerometer;

  //if stage is 'clamping' but flow has started
  if (curFlow > 0 && stage == 0) {
    //set stage to 'injecting - filling'
    stage = 1;
    updateStage();
    stageStartMillis = millis();
  }

  lastFlow = curFlow;

  //if stage is filling, pressure is under the maximum packing pressure (pressure has already reduced) and the stage has been active for over the minimum filling time
  if (stage == 1 && curPressure < maxPackPressure && stageMillis > minFillMillis) {
    //set stage to 'injecting - packing'
    stage = 2;
    updateStage();
    stageStartMillis = millis();
  }
/*
  //if flow is off-specs AND no bad parameters were detected previously during filling process
  if (correctParameters && (curFlow > maxInjectionFlow || curFlow < minInjectionFlow) && stage == 1 && stageMillis < minFillMillis - 1500) {
    //set correctParameters to false
    correctParameters = false;
    updateCorrectParameters();
  }

  //Check if filling is within spec, given a 5s interval to lower pressure for packing
  if (correctParameters && stage == 1 && ((curPressure > maxFillPressure || curPressure < minFillPressure) && stageMillis < maxFillMillis - (maxFillMillis * 0.33))) {
    correctParameters = false;
    Serial.print("Stage millis: ");
    Serial.println(stageMillis);
    updateCorrectParameters();
  }
*/
  if (correctParameters && stage == 1 && (((curFlow > maxInjectionFlow || curFlow < minInjectionFlow) && stageMillis < minFillMillis - 1500) || ((curPressure > maxFillPressure || curPressure < minFillPressure || curPlasticTemp > maxPlasticTemp || curPlasticTemp < minPlasticTemp) && stageMillis < 0.66 * maxFillMillis))) {
    correctParameters = false;
    updateCorrectParameters();
  }

  //if stage is packing but cavity temperature has started to cool down
  if (curCavityTemp < curPlasticTemp * 0.95 && curCavityTemp < lastCavityTemp && stage == 2) {
    //set stage to cooling
    stage = 3;
    updateStage();

    //if cooling started too early or too late
    if (stageMillis < minPackMillis || stageMillis > maxPackMillis) {
      correctParameters = false;
      updateCorrectParameters();
    }

    stageStartMillis = millis();
  }

  //Check if packing pressure is within specs
  if (stage == 2 && (curPressure < minPackPressure || curPressure > maxPackPressure)) {
    correctParameters = false;
    updateCorrectParameters();
  }

  //update last cavity temperature
  lastCavityTemp = curCavityTemp;
}

void updateCorrectParameters() {
  if (Firebase.RTDB.setBool(&fbdo, "molds/" + mold_id + "/currentParameters/isAcceptingParts", correctParameters)){
      //Serial.print("UPDATED PARAMETERS: ");
      //Serial.println(correctParameters);
      //Serial.println("PATH: " + fbdo.dataPath());
      //Serial.println("TYPE: " + fbdo.dataType());
  }
  else {
    Serial.println("FAILED UPDATING PARAMETERS");
    Serial.println("REASON: " + fbdo.errorReason());
  }
}

void updateCavityTemperature(){
  if (Firebase.RTDB.setInt(&fbdo, "molds/" + mold_id + "/currentParameters/cavityTempC", curCavityTemp)){
      //Serial.println("UPDATED CAVITY TEMP");
      //Serial.println("PATH: " + fbdo.dataPath());
      //Serial.println("TYPE: " + fbdo.dataType());
  }
  else {
    Serial.println("FAILED UPDATING CAVITY TEMP");
    Serial.println("REASON: " + fbdo.errorReason());
  }
}

void updatePressure(){
  if (Firebase.RTDB.setInt(&fbdo, "molds/" + mold_id + "/currentParameters/pressure", curPressure)){
      //Serial.println("UPDATED PRESSURE");
      //Serial.println("PATH: " + fbdo.dataPath());
      //Serial.println("TYPE: " + fbdo.dataType());
  }
  else {
    Serial.println("FAILED UPDATING PRESSURE");
    Serial.println("REASON: " + fbdo.errorReason());
  }
}

void updateFlow(){
  if (Firebase.RTDB.setInt(&fbdo, "molds/" + mold_id + "/currentParameters/injectionFlow", curFlow)){
      //Serial.println("UPDATED INJECTION FLOW");
      //Serial.println("PATH: " + fbdo.dataPath());
      //Serial.println("TYPE: " + fbdo.dataType());
  }
  else {
    Serial.println("FAILED UPDATING INJECTION FLOW");
    Serial.println("REASON: " + fbdo.errorReason());
  }
}

void updatePlasticTemp(){
  if (Firebase.RTDB.setInt(&fbdo, "molds/" + mold_id + "/currentParameters/plasticTempC", curPlasticTemp)){
      //Serial.println("UPDATED PLASTIC TEMP");
      //Serial.println("PATH: " + fbdo.dataPath());
      //Serial.println("TYPE: " + fbdo.dataType());
  }
  else {
    Serial.println("FAILED UPDATING PLASTIC TEMP");
    Serial.println("REASON: " + fbdo.errorReason());
  }
}

void updateStage(){
  String stageText = "";
  switch(stage) {
    case 0:
      stageText = "Clamping";
      break;
    case 1:
      stageText = "Injecting - Filling";
      break;
    case 2:
      stageText = "Injecting - Packing";
      break;
    case 3:
      stageText = "Cooling";
      break;
    case 4:
      stageText = "Ejecting";
      break;
  }

  if (Firebase.RTDB.setString(&fbdo, "molds/" + mold_id + "/currentParameters/stage", stageText)){
      Serial.println("UPDATED STAGE: " + stageText);
      //Serial.println("PATH: " + fbdo.dataPath());
      //Serial.println("TYPE: " + fbdo.dataType());
  }
  else {
    Serial.println("FAILED UPDATING STAGE");
    Serial.println("REASON: " + fbdo.errorReason());
  }
}

void incrementPartsProduced() {
  Firebase.RTDB.getInt(&fbdo, "molds/" + mold_id + "/totalPartsProduced");
  int totalPartsProduced = fbdo.to<int>();
  totalPartsProduced++;

  if (Firebase.RTDB.setInt(&fbdo, "molds/" + mold_id + "/totalPartsProduced", totalPartsProduced)) {
    Serial.print("INCREMENTED TOTAL PARTS PRODUCED: ");
    Serial.println(totalPartsProduced);
  }
  else {
    Serial.println("FAILED INCREMENTING TOTAL PARTS PRODUCED");
    Serial.println("REASON: " + fbdo.errorReason());
  }
}

void getOverride() {
  String path = "molds/" + mold_id + "/currentParameters/overrideUser";

  Firebase.RTDB.getBool(&fbdo, path);
  overrideUser = fbdo.to<bool>();
}

void getManufacturingParameters() {
  String basePath = "molds/" + mold_id + "/manufacturingParameters/";

  Firebase.RTDB.getInt(&fbdo, basePath + "cavityTemp/max");
  maxCavityTemp = fbdo.to<int>();

  Firebase.RTDB.getInt(&fbdo, basePath + "coolingTemp/max");
  maxCoolingTemp = fbdo.to<int>();
  Firebase.RTDB.getInt(&fbdo, basePath + "coolingTemp/min");
  minCoolingTemp = fbdo.to<int>();

  Firebase.RTDB.getInt(&fbdo, basePath + "coolingTime/max");
  maxCoolingMillis = fbdo.to<int>() * 1000;
  Firebase.RTDB.getInt(&fbdo, basePath + "coolingTime/min");
  minCoolingMillis = fbdo.to<int>() * 1000;

  Firebase.RTDB.getInt(&fbdo, basePath + "fillPressure/max");
  maxFillPressure = fbdo.to<int>();
  Firebase.RTDB.getInt(&fbdo, basePath + "fillPressure/min");
  minFillPressure = fbdo.to<int>();

  Firebase.RTDB.getInt(&fbdo, basePath + "fillTime/max");
  maxFillMillis = fbdo.to<int>() * 1000;
  Firebase.RTDB.getInt(&fbdo, basePath + "fillTime/min");
  minFillMillis = fbdo.to<int>() * 1000;

  Firebase.RTDB.getInt(&fbdo, basePath + "holdPressure/max");
  maxHoldPressure = fbdo.to<int>();
  Firebase.RTDB.getInt(&fbdo, basePath + "holdPressure/min");
  minHoldPressure = fbdo.to<int>();

  Firebase.RTDB.getInt(&fbdo, basePath + "packPressure/max");
  maxPackPressure = fbdo.to<int>();
  Firebase.RTDB.getInt(&fbdo, basePath + "packPressure/min");
  minPackPressure = fbdo.to<int>();

  Firebase.RTDB.getInt(&fbdo, basePath + "packTime/max");
  maxPackMillis = fbdo.to<int>() * 1000;
  Firebase.RTDB.getInt(&fbdo, basePath + "packTime/min");
  minPackMillis = fbdo.to<int>() * 1000;

  Firebase.RTDB.getInt(&fbdo, basePath + "injectionFlow/max");
  maxInjectionFlow = fbdo.to<int>();
  Firebase.RTDB.getInt(&fbdo, basePath + "injectionFlow/min");
  minInjectionFlow = fbdo.to<int>();

  Firebase.RTDB.getInt(&fbdo, basePath + "plasticTemp/max");
  maxPlasticTemp = fbdo.to<int>();
  Firebase.RTDB.getInt(&fbdo, basePath + "plasticTemp/min");
  minPlasticTemp = fbdo.to<int>();

  Serial.print("MinFillMillis: ");
  Serial.println(minFillMillis);
}

void getDayStr() {
  if (timeStatus() != timeNotSet) {
    if (day() != today) { //update the display only if time has changed
      prevDay = today;
      today = day();
      String dayStr = today < 10 ? String(0) + String(today) : String(today);
      String monthStr = month() < 10 ? String(0) + String(month()) : String(month());
      curDateStr = dayStr + monthStr + String(year());
    }
  }
}

/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  IPAddress ntpServerIP; // NTP server's ip address

  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  // get a random server from the pool
  WiFi.hostByName(ntpServerName, ntpServerIP);
  Serial.print(ntpServerName);
  Serial.print(": ");
  Serial.println(ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}