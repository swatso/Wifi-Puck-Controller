#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <PubSubClient.h>
#include <Servo.h>

#define SERVO_1_PIN D1
#define SERVO_2_PIN D2
#define SERVO_POWER_PIN D5

#define SERVO_1_OFF_US 1000
#define SERVO_1_ON_US 2000
#define SERVO_2_OFF_US 1000
#define SERVO_2_ON_US 2000

namespace {
constexpr unsigned long moveDurationMs = 2000;
constexpr unsigned long reconnectIntervalMs = 5000;
constexpr unsigned long batteryIntervalMs = 60000;
const char *const attachTopic = "track/turnout/node28/Attach";
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
  }
}

void onMessage(char *, byte *payload, unsigned int length) {
  unsigned long now = millis();
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
  digitalWrite(SERVO_POWER_PIN, LOW);
  pinMode(SERVO_POWER_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  servos[0].servo.attach(SERVO_1_PIN);
  servos[1].servo.attach(SERVO_2_PIN);
  for (auto &state : servos) {
    state.servo.writeMicroseconds(state.off);
  }
  digitalWrite(SERVO_POWER_PIN, HIGH);

  if (LittleFS.begin() &&
      readConfig("/host.txt", hostname, 32) && hostname.length() > 0 &&
      readConfig("/ssid.txt", ssid, 32) && ssid.length() > 0 &&
      readConfig("/pass.txt", password, 64)) {
    wifiConfigured = true;
    WiFi.mode(WIFI_STA);
    WiFi.hostname(hostname);
    connectWifi();
  }

  mqtt.setServer(mqttServer, 1883);
  mqtt.setCallback(onMessage);
}

void loop() {
  unsigned long now = millis();
  updateServos(now);

  bool wifiConnected = WiFi.status() == WL_CONNECTED;
  digitalWrite(LED_BUILTIN, wifiConnected ? LOW : HIGH);
  if (!wifiConnected) {
    if (wifiConfigured && now - lastWifiAttempt >= reconnectIntervalMs) {
      connectWifi();
    }
    return;
  }

  if (!mqtt.connected()) {
    if (!moving && now - lastMqttAttempt >= reconnectIntervalMs) {
      lastMqttAttempt = now;
      if (mqtt.connect(hostname.c_str())) {
        if (!mqtt.subscribe(attachTopic)) {
          mqtt.disconnect();
        }
      }
    }
    return;
  }

  mqtt.loop();
  now = millis();
  if (now - lastBatteryPublish >= batteryIntervalMs) {
    char battery[6];
    snprintf(battery, sizeof(battery), "%d", analogRead(A0));
    mqtt.publish(batteryTopic, battery);
    lastBatteryPublish = now;
  }
}
