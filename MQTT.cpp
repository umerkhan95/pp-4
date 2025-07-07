#include "freertos/projdefs.h"
#include "MQTT.h"
#include <PubSubClient.h>
#include "WiFiClient.h"
#include "WIFI_Class.h"

// ===== MQTT Broker =====
const char* mqtt_broker;
int mqtt_port;
const char* mqtt_sub_topic;  // Use to receive message from App/MQTT broker
const char* mqtt_pub_topic;  // Use to send message to App/MQTT broker

WiFiClient espClient;
PubSubClient client(espClient);

MQTTMessageHandler MQTT::userMessageHandler = nullptr;

TaskHandle_t MQTT::checkMQTTConnection = NULL;

void MQTT::MQTT_init(const char* publish_topic, const char* subscribe_topic, const char* broker, const int port) {
  mqtt_pub_topic = publish_topic;
  mqtt_sub_topic = subscribe_topic;
  mqtt_broker = broker;
  mqtt_port = port;
  client.setServer(mqtt_broker, mqtt_port);
  if (!client.connected()) reconnectMQTT();
  client.setCallback(mqttCallback);
  client.publish(mqtt_pub_topic, "Connected to MQTT");

  xTaskCreate(MQTT::check_mqtt_connection, "Check MQTT connection", 4096, NULL, 3, &checkMQTTConnection);
}

void MQTT::check_mqtt_connection(void *param) {
  for (;;) {
    if (!client.connected() && WIFI::getWifiStatus()) {
      Serial.println("Reconnecting MQTT...");
      reconnectMQTT();
    }
    client.loop();
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void MQTT::reconnectMQTT(void) {
  String clientId = "esp32-client-" + String(random(0xffff), HEX);
  int entry = 0;
  while (!client.connected() && entry < 3) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(clientId.c_str())) {
      Serial.println("MQTT connected");
      client.subscribe(mqtt_sub_topic); // Sub vào topic
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(". Going to try again");
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    entry++;
  }
}

void MQTT::mqttCallback(char *topic, byte *payload, unsigned int length) {
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);
  char body[512];
  memcpy(body, payload, length);
  body[length] = '\0';
  Serial.print("Message: ");
  Serial.println(body);
  Serial.println("-----------------------");

  StaticJsonDocument<512> jsonDocument;
  DeserializationError error = deserializeJson(jsonDocument, body);
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  if (userMessageHandler) {  // Gọi callback xử lý nội dung
    userMessageHandler(jsonDocument);
  }
}

void MQTT::setMessageHandler(MQTTMessageHandler handler) {
  userMessageHandler = handler;
}

void MQTT::publishMessage(const char* publish_topic, const char* message) {
  mqtt_pub_topic = publish_topic;
  client.publish(mqtt_pub_topic, message);
}