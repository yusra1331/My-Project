#include <WiFi.h>
#include <WiFiClient.h>
#include "ThingSpeak.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <BH1750.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Firebase_ESP_Client.h>
#include "time.h"

// Provide the token generation process info.
#include "addons/TokenHelper.h"
// Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"

#define ONE_WIRE_BUS 33    // Data pin for DS18B20 sensor
#define MSensor1_PIN 35    // ESP32 pin GIOP34 (ADC0) that connects to AOUT pin of the moisture sensor
#define RelayPump1_PIN 26  // ESP32 pin GIOP32 (ADC0) that connects to Relay 1
#define THRESHOLD 50       // CHANGE YOUR THRESHOLD HERE

#define API_KEY "xxxxxx"
#define USER_EMAIL "xxxxxxx"
#define USER_PASSWORD "xxxxxxxx"
#define DATABASE_URL "https://fyp3-4bcb1-default-rtdb.asia-southeast1.firebasedatabase.app/"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
BH1750 lightMeter;

// Replace with your network credentials
const char* ssid = "xxxxxxxxx";
const char* password = "xxxxxxxxxx";

WiFiServer server(80);
WiFiClient  client;

unsigned long myChannelNumber = 5;
const char * myWriteAPIKey = "xxxxxxxxxx";   //  Enter your Write API key from ThingSpeak

unsigned long channelId = 2394743;
unsigned long previousMillis = 0;
const long interval = 2000;
int botRequestDelay = 1000; // Change your bot request delay here (earlier is 1000)
unsigned long lastTimeBotRan;
String uid;
String databasePath;
String SoilPath = "/soilMoisture1";
String TempPath = "/currentTemperature";
String LuxPath = "/lux";
String timePath = "/timestamp";
String parentPath;
int timestamp;
FirebaseJson json;

// Define the interval for sending soil moisture notifications (in milliseconds)
unsigned long soilMoistureNotificationInterval = 360000; // 1 hour(3600 000)
unsigned long lastSoilMoistureNotificationTime = 0;


const char* ntpServer = "pool.ntp.org";

int soilMoisture1;
float soilMoisturePercentage;
float currentTemperature;
float lux;
String Message="";

#define BOTtoken "xxxxxxxx" //telegram bot
#define CHAT_ID "xxxxx"

WiFiClientSecure client2;
UniversalTelegramBot bot(BOTtoken, client2);

unsigned long getTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return 0;
  }
  time(&now);
  return now;
}

void updateFirebase() {
  if (Firebase.ready()) {
    // Get current timestamp as time_t
    time_t rawtime = getTime();
    struct tm* timeinfo;
    char formattedTime[50];

    // Format the time to your desired format: YY:MM:DD, HH:MM:SS
    timeinfo = localtime(&rawtime);
    strftime(formattedTime, sizeof(formattedTime), "%y:%m:%d, %H:%M:%S", timeinfo);
    Serial.print("Formatted Time: ");
    Serial.println(formattedTime);

    parentPath = databasePath + "/" + String(formattedTime);

    json.set(SoilPath.c_str(), String(soilMoisturePercentage));
    json.set(TempPath.c_str(), String(currentTemperature));
    json.set(LuxPath.c_str(), String(lux));
    json.set(timePath, String(formattedTime));

    Serial.printf("Set JSON... %s\n", Firebase.RTDB.setJSON(&fbdo, parentPath.c_str(), &json) ? "ok" : fbdo.errorReason().c_str());
  }
}

unsigned long previousThingSpeakMillis = 0; // Add this line

// Handle what happens when you receive new messages
void handleNewMessages(int numNewMessages) {
  Serial.println("handleNewMessages");
  Serial.println(String(numNewMessages));

  for (int i=0; i<numNewMessages; i++) {
    // Chat id of the requester
    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != CHAT_ID){
      bot.sendMessage(chat_id, "Unauthorized user", "");
      continue;
    }
    
    // Print the received message
    String text = bot.messages[i].text;
    Serial.println(text);

    String from_name = bot.messages[i].from_name;

    if (text == "/Read_Soil_Moisture") {
      Message = "Soil Moisture Level Percentage: ";
      Message += String(soilMoisturePercentage);
      Message += "%";

      bot.sendMessage(chat_id, Message, "");      
    }

    if (text == "/Read_Light_Intensity") {
      Message = "Light Intensity Level: ";
      Message += String(lux);

      bot.sendMessage(chat_id, Message, ""); 
    }
    
    if (text == "/Read_Temperature") {
      Message = "Soil Temperature Level: ";
      Message += String(currentTemperature);

      bot.sendMessage(chat_id, Message, ""); 
    }
  }
}

void setup() {
  Serial.begin(115200); // Initialize serial communication

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }
  Serial.println("WiFi connected");

  client2.setCACert(TELEGRAM_CERTIFICATE_ROOT); // Add root certificate for api.telegram.org

  // Initialize Firebase
  config.api_key = API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.database_url = DATABASE_URL;
  Firebase.reconnectWiFi(true);
  fbdo.setResponseSize(4096);
  config.token_status_callback = tokenStatusCallback;
  config.max_token_generation_retry = 5;
  Firebase.begin(&config, &auth);

  Serial.println("Getting User UID");
  while ((auth.token.uid) == "") {
    Serial.print('.');
    delay(1000);
  }
  uid = auth.token.uid.c_str();
  Serial.print("User UID: ");
  Serial.println(uid);

  databasePath = "/UsersData/" + uid + "/readings1"; //i change

  // Initialize sensors
  Wire.begin();
  lightMeter.begin();
  sensors.begin();

  // Initialize pins
  pinMode(RelayPump1_PIN, OUTPUT);

  // Send welcome message to Telegram
  bot.sendMessage(CHAT_ID, "Bot started up", "");
  String welcome = "Welcome \n";
  welcome += "Use the following commands to control your outputs.\n\n";
  welcome += "/Read_Soil_Moisture to read soil moisture level\n";
  welcome += "/Read_Light_Intensity to read light intensity level\n";
  welcome += "/Read_Temperature to read temperature\n";
  bot.sendMessage(CHAT_ID, welcome, "");

  // Configure NTP time
  configTime(8 * 3600, 0, ntpServer);

  // Initialize ThingSpeak
  ThingSpeak.begin(client);

  // Additional setup code goes here
}

void loop() {
  unsigned long currentMillis = millis();

  // Update Firebase every 60 seconds
  if (currentMillis - previousMillis >= 60000) {
    updateFirebase();
    previousMillis = currentMillis;
  }

  // Check soil moisture level and send Telegram notifications
  if (currentMillis - lastTimeBotRan >= botRequestDelay) {
    sensors.requestTemperatures();
    currentTemperature = sensors.getTempCByIndex(0);
    lux = lightMeter.readLightLevel();
    soilMoisture1 = analogRead(MSensor1_PIN);
    soilMoisturePercentage = map(soilMoisture1, 4095, 3290, 0, 100);
    Serial.print("Soil moisture value: ");
    Serial.println(soilMoisture1);
    Serial.print("Soil moisture percentage: ");
    Serial.print(soilMoisturePercentage);
    Serial.println("%");

    Serial.print("Soil Temperature Level: ");
    Serial.print(currentTemperature);
    Serial.println(" Celsius");

    Serial.print("Lux Level: ");
    Serial.println(lux);

    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

    while(numNewMessages) {
      Serial.println("got response");
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      break;
    } 
  // Control the pump based on soil moisture level
    if (soilMoisturePercentage < THRESHOLD) {
      Serial.println("The soil is DRY");
      digitalWrite(RelayPump1_PIN, LOW);
    } else {
      Serial.println("The soil is WET");
      digitalWrite(RelayPump1_PIN, HIGH);
    }

    
   // Check if enough time has passed since the last soil moisture notification
    if (currentMillis - lastSoilMoistureNotificationTime >= soilMoistureNotificationInterval) {
      lastSoilMoistureNotificationTime = currentMillis;  // Update the last notification time

      // Send soil moisture notifications
      if (soilMoisturePercentage < 40) {
        bot.sendMessage(CHAT_ID, "Warning: Very dry soil! Moisture level is low.", "");
      } else if (soilMoisturePercentage > 150) {
        bot.sendMessage(CHAT_ID, "Warning: Very wet soil! Moisture level is high.", "");
      } else if (soilMoisturePercentage > 36 && soilMoisturePercentage < 130) {
        bot.sendMessage(CHAT_ID, "Notice: Moderate moisture levels in the soil.", "");
      }
    }

    lastTimeBotRan = currentMillis;
  }

  // ThingSpeak update every 15 seconds
  if (currentMillis - previousThingSpeakMillis >= 15000) {
    ThingSpeak.setField(1, soilMoisturePercentage);
    ThingSpeak.setField(2, currentTemperature);
    ThingSpeak.setField(3, lux);

    int x = ThingSpeak.writeFields(myChannelNumber,myWriteAPIKey);

    if(x == 200){
      Serial.println("Channel update successful.");
    }
    else{
      Serial.println("Problem updating channel. HTTP error code " + String(x));
    }

    previousThingSpeakMillis = currentMillis;
  }

  // ... (other tasks or checks)

  delay(50);
}
