#include  <ESP8266WiFi.h>
#include  <ESP8266WebServer.h> 
#include <ArduinoJson.h>     // Need To Install
#include <Firebase_ESP_Client.h>
//Provide the token generation process info.
#include "addons/TokenHelper.h"
//Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"
#include  "secrets.h"

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
double curAccelerometer = 0;
double curFlow = 0;
double curPressure = 0;
bool correctParameters = true;

bool acceptProd = true;
unsigned int stage = 0;
//Available stages: 0 = "clamping", 1 = "injecting", 2 = "cooling", 3 = "ejecting"

//Demo variables
unsigned long stageStartMillis = 0;
unsigned int demoStage = 0;
/*double lastCavityTempDemo = 30;
double lastAccelerometerDemo = 0;
double lastFlowDemo = 0;
double lastPressureDemo = 0;*/

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

  stageStartMillis = millis();
  //Get manufacturing parameters
  getManufacturingParameters();
}

void loop() {
  if (Firebase.ready() && signupOK) {
    demoMode();
    manageProduction();
    delay(1000);
  }
}

void demoMode() {
  unsigned long demoStageMillis = millis() - stageStartMillis;

  //Serial.println("DemoStageSecs=" + demoStageMillis);
  Serial.println("-------------------------------");
  Serial.print("DemoStageSecs = ");
  Serial.println(demoStageMillis);
  Serial.print("CurAccelerometer = ");
  Serial.println(curAccelerometer);
  Serial.print("CurFlow = ");
  Serial.println(curFlow);
  Serial.print("CurCavityTemp = ");
  Serial.println(curCavityTemp);

  switch(demoStage) {
    //Clamping
    case 0:
      Serial.println("Currently clamping");
      //Average duration is ~20s
      if (demoStageMillis > 20000) {
        curAccelerometer = 0;
        demoStage = 1;
        curFlow = 5;
        stageStartMillis = millis();
        return;
      }

      //Temperature, Flow and Pressure don't change
      if (demoStageMillis < 10000) {
        curAccelerometer -= 0.05;
      }
      else if ((demoStageMillis >= 10000 && demoStageMillis < 20000) || curAccelerometer < 0) {
        curAccelerometer += 0.05;
      }

      break;
    case 1:
      Serial.println("Currently injecting");

      if (demoStageMillis > 30000) {
        curFlow = 0;
        demoStage = 2;
        stageStartMillis = millis();
        return;
      }

      curFlow *= double(random(98, 102)) / 100;

      if (demoStageMillis < 10000) {
        curCavityTemp *= double(random(105, 115)) / 100;
      }
      else if (demoStageMillis >= 10000 && demoStageMillis < 30000) {
        curCavityTemp *= double(random(102, 110)) / 100;
      }

      break;
    case 2:
      Serial.println("Currently cooling");

      if (curCavityTemp < maxCoolingTemp) {
        demoStage = 3;
        stageStartMillis = millis();
        return;
      }

      curCavityTemp *= double(random(90, 98)) / 100;

      break;
    case 3:
      Serial.println("Currently ejecting");
      break;
  }
}

void manageProduction() {
  //send current data to database
  updateCavityTemperature();
  updateFlow();

  //if mold is opening
  if (curAccelerometer > 0 && lastAccelerometer < curAccelerometer && stage == 2) {
    //set stage to 'ejecting'
    stage = 3;
    updateStage();
  }
  //or if mold is clamping
  else if (curAccelerometer < 0 && lastAccelerometer > curAccelerometer && stage == 3) {
    //set stage to 'clamping'
    stage = 0;
    updateStage();
    //restart correctParameters boolean
    correctParameters = true;
  }

  //update last acceleration
  lastAccelerometer = curAccelerometer;

  //if flow is off-specs AND no bad parameters were detected previously
  if (correctParameters && (curFlow > maxFlow || curFlow < minFlow)) {
    //set correctParameters to false
    correctParameters = false;
  }

  //if stage is 'clamping' but flow has started
  if (curFlow > 0 && stage == 0) {
    //set stage to 'injecting'
    stage = 1;
    updateStage();
  }

  lastFlow = curFlow;

  //TODO implement pressure

  //if stage is injecting but cavity temperature has started to cool down
  if (curCavityTemp < lastCavityTemp && stage == 1) {
    //set stage to cooling
    stage = 2;
    updateStage();
  }
  //if cavity temperature has cooled below the maximum ejecting temperature
  else if (curCavityTemp < lastCavityTemp && curCavityTemp < maxCoolingTemp && stage == 2) {
    //set stage to ejecting
    stage = 3;
    updateStage();
  }

  //update last cavity temperature
  lastCavityTemp = curCavityTemp;
}

void acceptParts() {
  if (Firebase.RTDB.setBool(&fbdo, "molds/1000/currentParameters/isAcceptingParts", correctParameters)){
      Serial.println("PASSED");
      //Serial.println("PATH: " + fbdo.dataPath());
      //Serial.println("TYPE: " + fbdo.dataType());
  }
  else {
    Serial.println("FAILED");
    Serial.println("REASON: " + fbdo.errorReason());
  }
}

void updateCavityTemperature(){
  if (Firebase.RTDB.setInt(&fbdo, "molds/1000/currentParameters/tempC", curCavityTemp)){
      Serial.println("UPDATED CAVITY TEMP");
      //Serial.println("PATH: " + fbdo.dataPath());
      //Serial.println("TYPE: " + fbdo.dataType());
  }
  else {
    Serial.println("FAILED UPDATING CAVITY TEMP");
    Serial.println("REASON: " + fbdo.errorReason());
  }
}

void updateFlow(){
  if (Firebase.RTDB.setInt(&fbdo, "molds/1000/currentParameters/injectionFlow", curFlow)){
      Serial.println("UPDATED INJECTION FLOW");
      //Serial.println("PATH: " + fbdo.dataPath());
      //Serial.println("TYPE: " + fbdo.dataType());
  }
  else {
    Serial.println("FAILED UPDATING INJECTION FLOW");
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
      stageText = "Injecting";
      break;
    case 2:
      stageText = "Cooling";
      break;
    case 3:
      stageText = "Ejecting";
      break;
  }

  if (Firebase.RTDB.setString(&fbdo, "molds/1000/currentParameters/stage", stageText)){
      Serial.println("UPDATED STAGE");
      //Serial.println("PATH: " + fbdo.dataPath());
      //Serial.println("TYPE: " + fbdo.dataType());
  }
  else {
    Serial.println("FAILED UPDATING STAGE");
    Serial.println("REASON: " + fbdo.errorReason());
  }
void getManufacturingParameters() {
  String basePath = "molds/1000/manufacturingParameters/";

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