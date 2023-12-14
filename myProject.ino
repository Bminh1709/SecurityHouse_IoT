#define BLYNK_TEMPLATE_ID "TMPL6bZp2avSF"
#define BLYNK_TEMPLATE_NAME "Quickstart Template"
#define BLYNK_AUTH_TOKEN "-k_JxaBpfmpER7SD5cZswJm-JFZRDG9F"

// Blynk and WifiManager
#define BLYNK_PRINT Serial
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <BlynkSimpleEsp8266.h>

// Components
#include <Servo.h>
#include <SPI.h>
#include <MFRC522.h>
#include <DHT.h>

// Firebase
#include <Firebase_ESP_Client.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#define API_KEY "AIzaSyCPsKYhZx427i0gktm2CuwBVaSzPBHQnAA"
#define DATABASE_URL "https://minh-iot-house-default-rtdb.asia-southeast1.firebasedatabase.app/"

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
FirebaseJson json;

// PIN configuration
WidgetLED led(V2);
const int btnPin = D2;
const int motionPin = D0;
const int ledPin = 3;
const int servoPin = D8;
int pos = 0;
bool isAuth = false;
constexpr uint8_t RST_PIN = D3;
constexpr uint8_t SS_PIN = D4;

Servo myservo;
MFRC522 rfid(SS_PIN, RST_PIN); // Instance of the class
MFRC522::MIFARE_Key key;
String tag;
String memberName;

enum DoorState { CLOSE, OPEN };

DoorState currentDoorState = CLOSE;
unsigned long motionDetectedTime = 0;
const unsigned long motionTimeout = 15000;

WiFiManager wifiMn;
bool signupOK = false;
#define DHTPIN D1
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

void setup()
{
  Serial.begin(115200);
  timeClient.begin();
  timeClient.setTimeOffset(25200);

  pinMode(motionPin, INPUT);
  pinMode(ledPin, FUNCTION_3);
  pinMode(ledPin, OUTPUT);
  pinMode(btnPin, INPUT);

  dht.begin(); // DHT11
  myservo.attach(servoPin); // Servo
  myservo.write(pos);
  SPI.begin(); // RFID
  rfid.PCD_Init();

  // wifiMn.resetSettings();
  if (!wifiMn.autoConnect("Bui Minh")) {
    Serial.println("Failed to connect and hit timeout");
    ESP.reset();
    delay(1000);
  } Serial.println("Connected to Wi-Fi!");

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  if (Firebase.signUp(&config, &auth, "", "")) {
      Serial.println("Authentication successful");
      signupOK = true;
  } else {
      Serial.printf("Authentication failed: %s\n", config.signer.signupError.message.c_str());
  }

  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Blynk
  Blynk.config(BLYNK_AUTH_TOKEN);
  Blynk.connect();
}

// Path for DHT
String dhtPath = "/DHT11";
String humidityPath = "/humidity";
String temperaturePath = "/temperature";
String DhtParentPath;
// Path for Motion
String motionPath = "/House";
String memberPath = "/Member";
String doorStatePath = "/Door";
String houseParentPath;

void loop()
{
  Blynk.run();
  
  int btnState = digitalRead(btnPin);
  if (btnState == HIGH) {

      memberName = "";
      tag = "";

      if (checkRFID()) { 
        openDoor();
        SendMotionData();
        delay(3000);
      }

      int motionState = digitalRead(motionPin);
      Serial.println(motionState);
      if (motionState == HIGH)
      {
        Serial.println("Motion detected");
        motionDetectedTime = millis(); 
        // Turn on the led
        digitalWrite(ledPin, HIGH);
        led.setValue(255);
        // --------------
        if (currentDoorState == OPEN) { closeDoor(); }

        isAuth = false; 
        memberName = "";
        tag = ""; 

        while (millis() - motionDetectedTime < motionTimeout)
        {
          if (checkRFID())
          {
            Serial.println("You're in, open the door");
            digitalWrite(ledPin, LOW);
            led.setValue(0);
            openDoor();
            isAuth = true; 
            break;      
          }
        }

        if (!isAuth)
        {
          closeDoor();
        }

        SendMotionData();

        digitalWrite(ledPin, LOW);
        led.setValue(0);
        delay(4000);
      }
  } else {
      delay(1000);

      float humidity = dht.readHumidity();
      float temperature = dht.readTemperature();

      if (isnan(humidity) || isnan(temperature)) 
      {
          Serial.println(F("Failed to read from DHT sensor!"));
          Blynk.virtualWrite(V0, 0);
          Blynk.virtualWrite(V1, 0);
          return;
      }

      Blynk.virtualWrite(V0, temperature);
      Blynk.virtualWrite(V1, humidity);

      // Serial.print("Humidity: ");
      // Serial.print(humidity);
      // Serial.print(" %\t");
      // Serial.print("Temperature: ");
      // Serial.print(temperature);
      // Serial.println(" °C");

      if (Firebase.ready() && signupOK) {
          delay(5000);
          String datetime = getDatetime();
          DhtParentPath = dhtPath + "/" + datetime;
          // set the JSON strings for humidity and temperature
          json.set(humidityPath.c_str(), humidity);
          json.set(temperaturePath.c_str(), temperature);
          // send data to the real-time database
          if (Firebase.RTDB.setJSON(&fbdo, DhtParentPath.c_str(), &json)) {
              Serial.print("Humidity and Temperature path - ");
              Serial.println(fbdo.dataPath());
          } else {
              Serial.println("ERROR: " + fbdo.errorReason());
          }
      }
  }

}

void SendMotionData() {
  if (Firebase.ready() && signupOK) {
      String datetime = getDatetime();
      houseParentPath = motionPath + "/" + datetime;
      String curDoorState;
      if (currentDoorState == OPEN) { curDoorState = "Opening"; }
      else { curDoorState = "Closing"; }
      if (memberName == "") { memberName = "Stranger"; }
      json.set(memberPath.c_str(), memberName);
      json.set(doorStatePath.c_str(), curDoorState);
      // send data to the real-time database
      if (Firebase.RTDB.setJSON(&fbdo, houseParentPath.c_str(), &json)) {
          Serial.print("Member: " + memberName + " | Door state: " + curDoorState);
          Serial.println(fbdo.dataPath());
      } else {
          Serial.println("ERROR: " + fbdo.errorReason());
      }
  }
}

void openDoor()
{
  if (currentDoorState == CLOSE)
  {
    for (pos = 0; pos <= 150; pos += 1)
    {
      myservo.write(pos);
      delay(15);
    }
    currentDoorState = OPEN;
  }
}

void closeDoor()
{
  if (currentDoorState == OPEN)
  {
    for (pos = 150; pos >= 0; pos -= 1)
    {
      myservo.write(pos);
      delay(15);
    }
    currentDoorState = CLOSE;
  }
}

bool checkRFID()
{
  if (!rfid.PICC_IsNewCardPresent())
    return false;

  if (rfid.PICC_ReadCardSerial())
  {
    Serial.println("Card read successfully");

    for (byte i = 0; i < 4; i++)
    {
      tag += rfid.uid.uidByte[i];
    }

    if (tag == "1301052826")
    {
      Serial.println("Access successfully!");
      memberName = "Minh cute"; 
      tag = "";
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
      return true;
    }
    else if (tag == "249225790")
    {
      Serial.println("Access successfully!");
      memberName = "Mom"; 
      tag = "";
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
      return true;
    }
    else
    {
      Serial.println("Access denied!");
      tag = "";
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
      return false;
    }
  }

  return false;
}

String getDatetime() {
    timeClient.update();
    time_t epochTime = timeClient.getEpochTime();
    struct tm *ptm = gmtime((time_t *)&epochTime);
    return String(ptm->tm_mday) + "-" + String(ptm->tm_mon + 1) + "-" + String(ptm->tm_year + 1900) + " " + timeClient.getFormattedTime();
}
