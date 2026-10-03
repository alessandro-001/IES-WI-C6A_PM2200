#include "local_mqtt.h"

#include "config.h"
#include "web_server.h"
#include "rs485_sensor.h"
#include "pm2200.h"

#include <PubSubClient.h>
#include <WiFi.h>
#include <Preferences.h>
#include <math.h>
#include <time.h>
#include <ESPmDNS.h>

// Firmware MQTT target:
// ESP32-C6 -> Raspberry Pi MQTT broker -> mqtt_to_influx.py -> InfluxDB
//
// Topics published (payload contract: docs/payload.md):
// sensors/PM_<last4mac>/power        every 5 s
// sensors/PM_<last4mac>/attributes   retained, every 60 s and on reconnect
//
// The Docker bridge must subscribe to sensors/+/power (new topic, same envelope).

static WiFiClient localWifiClient;
static PubSubClient localMqtt(localWifiClient);

static String gBrokerHost = LOCAL_MQTT_SERVER;
static uint16_t gBrokerPort = LOCAL_MQTT_PORT;

static unsigned long lastReconnectAttempt = 0;
static unsigned long lastAttributesPublish = 0;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static String macNoColon() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  return mac;
}

static String getDeviceSuffix() {
  String mac = macNoColon();
  return mac.length() >= 4 ? mac.substring(mac.length() - 4) : mac;
}

static String getDeviceId() {
  return String(DEVICE_ID_PREFIX) + getDeviceSuffix();
}

static String buildDataTopic() {
  return "sensors/" + getDeviceId() + "/" + String(MQTT_MEASUREMENT);
}

static String buildAttributesTopic() {
  return "sensors/" + getDeviceId() + "/attributes";
}

static const char* boolText(bool v) {
  return v ? "true" : "false";
}

static String isoTimestampUtc() {
  time_t now = time(nullptr);

  // If NTP is not configured, return empty.
  // mqtt_to_influx.py will use server time when timestamp is missing.
  if (now < 1700000000) {
    return "";
  }

  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);

  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buf);
}

// Optional `"timestamp":"...",` fragment for the envelope (empty while NTP is not valid).
static void timestampField(char* out, size_t cap) {
  String timestamp = isoTimestampUtc();
  if (timestamp.length() > 0) {
    snprintf(out, cap, "\"timestamp\":\"%s\",", timestamp.c_str());
  } else {
    out[0] = '\0';
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Broker settings
// ─────────────────────────────────────────────────────────────────────────────

void localMqttSetBroker(const String& hostOrIp, uint16_t port) {
  if (hostOrIp.length() == 0) {
    Serial.println("[LocalMQTT] Empty broker host ignored");
    return;
  }

  gBrokerHost = hostOrIp;
  gBrokerPort = port;

  Preferences p;
  p.begin("broker", false);
  p.putString("host", gBrokerHost);
  p.putUShort("port", gBrokerPort);
  p.end();

  localMqtt.disconnect();
  localWifiClient.stop();

  // PubSubClient accepts hostname or IP string here.
  localMqtt.setServer(gBrokerHost.c_str(), gBrokerPort);

  Serial.printf("[LocalMQTT] Broker saved -> %s:%u\n",
                gBrokerHost.c_str(),
                gBrokerPort);
}

String localMqttGetBrokerIP() {
  Preferences p;
  p.begin("broker", true);

  // Support both new key "host" and older key "ip".
  String host = p.getString("host", "");
  if (host.length() == 0) {
    host = p.getString("ip", LOCAL_MQTT_SERVER);
  }

  p.end();
  return host;
}

uint16_t localMqttGetBrokerPort() {
  Preferences p;
  p.begin("broker", true);
  uint16_t port = p.getUShort("port", LOCAL_MQTT_PORT);
  p.end();
  return port;
}

static void loadBroker() {
  gBrokerHost = localMqttGetBrokerIP();
  gBrokerPort = localMqttGetBrokerPort();

  if (gBrokerHost.length() == 0) {
    gBrokerHost = LOCAL_MQTT_SERVER;
  }

  localMqtt.setServer(gBrokerHost.c_str(), gBrokerPort);

  Serial.printf("[LocalMQTT] Broker loaded -> %s:%u\n",
                gBrokerHost.c_str(),
                gBrokerPort);
}

// ─────────────────────────────────────────────────────────────────────────────
// Connection
// ─────────────────────────────────────────────────────────────────────────────

static bool resolveBrokerAddress(IPAddress& outIp) {
  // Prefer literal IPs to avoid resolver dependency.
  if (outIp.fromString(gBrokerHost)) {
    return true;
  }

  // .local names are mDNS; resolve through mDNS first when available.
  if (gBrokerHost.endsWith(".local")) {
    IPAddress mdnsIp = MDNS.queryHost(gBrokerHost.c_str());
    if (mdnsIp != INADDR_NONE) {
      outIp = mdnsIp;
      return true;
    }
  }

  // Fallback to regular DNS resolver.
  return WiFi.hostByName(gBrokerHost.c_str(), outIp);
}


static void localMqttConnect() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (localMqtt.connected()) {
    return;
  }

  String clientId = "ESP32C6-" + macNoColon();

  IPAddress brokerIp;
  if (!resolveBrokerAddress(brokerIp)) {
    Serial.printf("[LocalMQTT] Resolve failed for %s (wifi=%d rssi=%d)\n",
                  gBrokerHost.c_str(),
                  WiFi.status(),
                  WiFi.RSSI());
    logPush("[LocalMQTT] resolve failed host=" + gBrokerHost);
    return;
  }

  localMqtt.setServer(brokerIp, gBrokerPort);

  Serial.printf("[LocalMQTT] Connecting to %s (%s):%u as %s ... ",
                gBrokerHost.c_str(),
                brokerIp.toString().c_str(),
                gBrokerPort,
                clientId.c_str());


  bool ok = localMqtt.connect(clientId.c_str());

  if (ok) {
    Serial.println("connected");
    Serial.printf("[LocalMQTT] Data topic: %s\n", buildDataTopic().c_str());
    Serial.printf("[LocalMQTT] Attr topic: %s\n", buildAttributesTopic().c_str());

    logPush("[LocalMQTT] connected " + gBrokerHost + ":" + String(gBrokerPort));
    logPush("[LocalMQTT] topic " + buildDataTopic());

    // Force attributes after reconnect.
    lastAttributesPublish = 0;
  } else {
    Serial.printf("failed rc=%d wifi=%d rssi=%d ip=%s\n",
                  localMqtt.state(),
                  WiFi.status(),
                  WiFi.RSSI(),
                  WiFi.localIP().toString().c_str());

    logPush("[LocalMQTT] connect failed rc=" + String(localMqtt.state()));
    localWifiClient.stop();
  }
}

void localMqttInit() {
  loadBroker();

  localMqtt.setBufferSize(1280);   // power payload is ~0.7-1 KB, see docs/payload.md
  localMqtt.setKeepAlive(30);
  localMqtt.setSocketTimeout(5);

  localMqttConnect();

  Serial.printf("[LocalMQTT] Initialised -> %s:%u\n",
                gBrokerHost.c_str(),
                gBrokerPort);
}

void localMqttHandle() {
  if (WiFi.status() != WL_CONNECTED) {
    if (localMqtt.connected()) {
      localMqtt.disconnect();
    }
    return;
  }

  if (localMqtt.connected()) {
    localMqtt.loop();

    // Publish attributes every 60 seconds (retained).
    unsigned long now = millis();
    if (now - lastAttributesPublish >= 60000UL) {
      lastAttributesPublish = now;
      localMqttPublishAttributes();
    }

    return;
  }

  unsigned long now = millis();
  if (now - lastReconnectAttempt >= 5000UL) {
    lastReconnectAttempt = now;
    localWifiClient.stop();
    localMqttConnect();
  }
}

bool localMqttIsConnected() {
  return localMqtt.connected();
}

// ─────────────────────────────────────────────────────────────────────────────
// Publish power meter data
// ─────────────────────────────────────────────────────────────────────────────

void localMqttPublish() {
  if (!localMqtt.connected()) {
    return;
  }

  String deviceId = getDeviceId();
  String topic = buildDataTopic();

  char tsField[48];
  timestampField(tsField, sizeof(tsField));

  char payload[1100];
  int n = snprintf(
    payload,
    sizeof(payload),
    "{"
      "\"device_id\":\"%s\","
      "%s"
      "\"reading\":{"
        "\"sensor_type\":%u,"
        "\"sensor_type_label\":\"%s\","
        "\"firmware\":\"%s\","
        "\"rssi\":%d,"
        "\"sensor_ok\":%s,"
        "\"meter_ok\":%s,"
        "\"simulated\":%s",
    deviceId.c_str(),
    tsField,
    SENSOR_TYPE_ID,
    SENSOR_TYPE_LABEL,
    FIRMWARE_VERSION,
    WiFi.RSSI(),
    boolText(pm2200SensorOK),
    boolText(pm2200MeterOK),
    boolText(pm2200Simulated())
  );

  // Measurements: one table shared with the setup page (pm2200.cpp); keys without a value are left out.
  if (n >= 0 && n < (int)sizeof(payload)) {
    int m = pm2200ReadingJson(payload + n, sizeof(payload) - n);
    n = (m < 0) ? -1 : n + m;
  }
  if (n >= 0 && n + 3 <= (int)sizeof(payload)) n += snprintf(payload + n, sizeof(payload) - n, "}}");
  else n = -1;

  if (n < 0) {
    Serial.println("[LocalMQTT] Power payload too long — not published");
    logPush("[LocalMQTT] payload too long");
    return;
  }

  bool ok = localMqtt.publish(topic.c_str(), payload);

  if (ok) {
    Serial.printf("[LocalMQTT] Published to %s: %s\n",
                  topic.c_str(),
                  payload);
  } else {
    Serial.printf("[LocalMQTT] Publish failed topic=%s payloadLen=%u state=%d\n",
                  topic.c_str(),
                  strlen(payload),
                  localMqtt.state());

    logPush("[LocalMQTT] publish failed rc=" + String(localMqtt.state()));
    localWifiClient.stop();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Publish attributes (retained)
// ─────────────────────────────────────────────────────────────────────────────

void localMqttPublishAttributes() {
  if (!localMqtt.connected()) {
    return;
  }

  String deviceId = getDeviceId();
  String topic = buildAttributesTopic();

  char tsField[48];
  timestampField(tsField, sizeof(tsField));

  char payload[700];
  snprintf(
    payload,
    sizeof(payload),
    "{"
      "\"device_id\":\"%s\","
      "%s"
      "\"reading\":{"
        "\"mac\":\"%s\","
        "\"ip\":\"%s\","
        "\"rssi\":%d,"
        "\"firmware\":\"%s\","
        "\"sensor_type\":%u,"
        "\"sensor_type_label\":\"%s\","
        "\"meter_model\":\"PM2200\","
        "\"modbus_addr\":%u,"
        "\"baud\":%lu,"
        "\"parity\":\"%c\""
      "}"
    "}",
    deviceId.c_str(),
    tsField,
    macNoColon().c_str(),
    WiFi.localIP().toString().c_str(),
    WiFi.RSSI(),
    FIRMWARE_VERSION,
    SENSOR_TYPE_ID,
    SENSOR_TYPE_LABEL,
    pm2200Addr(),
    (unsigned long)pm2200Baud(),
    pm2200Parity()
  );

  bool ok = localMqtt.publish(topic.c_str(), payload, true);

  if (ok) {
    Serial.printf("[LocalMQTT] Attributes published to %s: %s\n",
                  topic.c_str(),
                  payload);
  } else {
    Serial.printf("[LocalMQTT] Attributes publish failed topic=%s payloadLen=%u state=%d\n",
                  topic.c_str(),
                  strlen(payload),
                  localMqtt.state());

    logPush("[LocalMQTT] attributes publish failed rc=" + String(localMqtt.state()));
    localWifiClient.stop();
  }
}
