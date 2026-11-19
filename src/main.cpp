#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <PubSubClient.h>
#include <Servo.h>
#include <cstring>

#define SERVO_1_PIN D1
#define SERVO_2_PIN D2
#define SERVO_POWER_PIN D5
#define ALIVE_PIN D6

#define SERVO_1_OFF_US 1000
#define SERVO_1_ON_US 2000
#define SERVO_2_OFF_US 1000
#define SERVO_2_ON_US 2000

namespace {
constexpr unsigned long moveDurationMs = 2000;
constexpr unsigned long reconnectIntervalMs = 5000;
constexpr unsigned long batteryIntervalMs = 60000;
constexpr unsigned long keepAliveTimeoutMs = 5UL * 60UL * 1000UL;
const char *const attachTopic = "track/turnout/node2B/Attach";
const char *const keepAliveTopic = "track/turnout/node2B/KeepAlive";
const char *const batteryTopic = "track/sensor/node2B/Battery";
const IPAddress mqttServer(192, 168, 0, 2);

struct ServoState {
  Servo servo;
  int current;
  int start;
  int target;
  int off;
  int on;
};

ServoState servos[] = {
    {Servo(), SERVO_1_OFF_US, SERVO_1_OFF_US, SERVO_1_OFF_US,
     SERVO_1_OFF_US, SERVO_1_ON_US},
    {Servo(), SERVO_2_OFF_US, SERVO_2_OFF_US, SERVO_2_OFF_US,
     SERVO_2_OFF_US, SERVO_2_ON_US},
};

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
String hostname;
String ssid;
String password;
bool wifiConfigured = false;
bool moving = false;
unsigned long moveStarted = 0;
unsigned long lastWifiAttempt = 0;
unsigned long lastMqttAttempt = 0;
unsigned long lastBatteryPublish = 0;
unsigned long lastKeepAlive = 0;
bool alive = true;

bool updateAlive(unsigned long now) {
  if (alive && now - lastKeepAlive >= keepAliveTimeoutMs) {
    digitalWrite(ALIVE_PIN, LOW);
    alive = false;
    Serial.println("Keep-alive timeout; ALIVE off.");
  }
  return alive;
}

bool readConfig(const char *path, String &value, size_t maxLength) {
  File file = LittleFS.open(path, "r");
  if (!file || file.size() > maxLength + 2) {
    return false;
  }
  value = file.readString();
  while (value.endsWith("\n") || value.endsWith("\r")) {
    value.remove(value.length() - 1);
  }
  return value.length() <= maxLength;
}

void updateServos(unsigned long now) {
  if (!moving) {
    return;
  }
  unsigned long elapsed = now - moveStarted;
  if (elapsed >= moveDurationMs) {
    elapsed = moveDurationMs;
    moving = false;
  }
  for (auto &state : servos) {
    state.current = state.start +
                    (state.target - state.start) * static_cast<long>(elapsed) /
                        static_cast<long>(moveDurationMs);
    state.servo.writeMicroseconds(state.current);
    Serial.print("Servo current: ");
    Serial.println(state.current);
  }
}

void onMessage(char *topic, byte *payload, unsigned int length) {
  unsigned long now = millis();
  if (!updateAlive(now)) {
    return;
  }
  if (std::strcmp(topic, keepAliveTopic) == 0) {
    if (length > 0 && payload[0] == 'T') {
      lastKeepAlive = now;
    }
    return;
  }
  if (std::strcmp(topic, attachTopic) != 0) {
    return;
  }
  updateServos(now);
  bool raise = length > 0 && payload[0] == 'T';
  for (auto &state : servos) {
    state.start = state.current;
    state.target = raise ? state.on : state.off;
  }
  moveStarted = now;
  moving = true;
}

void connectWifi() {
  WiFi.begin(ssid.c_str(), password.c_str());
  lastWifiAttempt = millis();
}
}  // namespace

void setup() {
  digitalWrite(ALIVE_PIN, HIGH);
  pinMode(ALIVE_PIN, OUTPUT);
  lastKeepAlive = millis();
  digitalWrite(SERVO_POWER_PIN, LOW);
  pinMode(SERVO_POWER_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(9600);
  servos[0].servo.attach(SERVO_1_PIN, 200, 3000, SERVO_1_OFF_US);
  servos[1].servo.attach(SERVO_2_PIN, 200, 3000, SERVO_2_OFF_US);
  digitalWrite(SERVO_POWER_PIN, HIGH);

  Serial.println("Starting setup...");
  if (LittleFS.begin() &&
      readConfig("/host.txt", hostname, 32) && hostname.length() > 0 &&
      readConfig("/ssid.txt", ssid, 32) && ssid.length() > 0 &&
      readConfig("/pass.txt", password, 64)) {
    Serial.println("Configuration read successfully.");
    wifiConfigured = true;
    WiFi.mode(WIFI_STA);
    WiFi.hostname(hostname);
    connectWifi();
  } else {
    Serial.println("Failed to read configuration.");
  }

  mqtt.setServer(mqttServer, 1883);
  mqtt.setCallback(onMessage);
}

void loop() {
  unsigned long now = millis();
  if (!updateAlive(now)) {
    return;
  }
  updateServos(now);

  bool wifiConnected = WiFi.status() == WL_CONNECTED;
  digitalWrite(LED_BUILTIN, wifiConnected ? LOW : HIGH);
  if (!wifiConnected) {
    if (wifiConfigured && now - lastWifiAttempt >= reconnectIntervalMs) {
      Serial.print(".");

      connectWifi();
    }
    return;
  }

  if (!mqtt.connected()) {
    if (!moving && now - lastMqttAttempt >= reconnectIntervalMs) {
      lastMqttAttempt = now;
      if (mqtt.connect(hostname.c_str())) {
        if (!mqtt.subscribe(attachTopic) || !mqtt.subscribe(keepAliveTopic)) {
          Serial.println("MQTT subscription failed.");
          mqtt.disconnect();
        }
      }
    }
    return;
  }

  mqtt.loop();
  if (!alive) {
    return;
  }
  now = millis();
  if (now - lastBatteryPublish >= batteryIntervalMs) {
    char battery[6];
    snprintf(battery, sizeof(battery), "%d", analogRead(A0));
    mqtt.publish(batteryTopic, battery);
    lastBatteryPublish = now;
  }
}
