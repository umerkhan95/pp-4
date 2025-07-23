#include "MQTT.h"
#include <PubSubClient.h>
#include "WiFiClientSecure.h"
#include "WIFI_Class.h"

// ===== MQTT Broker =====
const char* mqtt_broker;
int mqtt_port;
const char* mqtt_sub_topic;  // Use to receive message from App/MQTT broker
const char* mqtt_pub_topic;  // Use to send message to App/MQTT broker

WiFiClientSecure secureClient;
PubSubClient client(secureClient);


MQTTMessageHandler MQTT::userMessageHandler = nullptr;

TaskHandle_t MQTT::checkMQTTConnection = NULL;

void MQTT::MQTT_init(const char* broker, const int port, const char* username, const char* password) {
    mqtt_broker = broker;
    mqtt_port = port;
    secureClient.setInsecure();
    client.setServer(mqtt_broker, mqtt_port);
    client.setCallback(mqttCallback);
    client.setSocketTimeout(10);
    
    // Tăng stack size cho task check connection
    xTaskCreate(check_mqtt_connection, 
                "Check MQTT connection", 
                8192,  // Tăng stack size
                NULL, 
                2, 
                &checkMQTTConnection);
}

void MQTT::check_mqtt_connection(void *param) {
    const TickType_t xDelay = pdMS_TO_TICKS(5000);
    
    // Đợi WiFi kết nối thành công
    while(WiFi.status() != WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    // Đợi thêm 2s sau khi WiFi kết nối
    // vTaskDelay(pdMS_TO_TICKS(2000));
    
    for (;;) {
        if (WiFi.status() == WL_CONNECTED) {
            if(!client.connected()) {
                static uint32_t lastReconnectAttempt = 0;
                uint32_t now = millis();
                
                if(now - lastReconnectAttempt > 5000) {
                    lastReconnectAttempt = now;
                    // Thử reconnect trong task riêng để tránh stack overflow
                    xTaskCreate([](void* parameter) {
                        reconnectMQTT();
                        vTaskDelete(NULL);
                    }, "MQTT_Reconnect", 4096, NULL, 1, NULL);
                }
            } else {
                // Nếu đã kết nối, duy trì kết nối
                if(!client.loop()) {
                    Serial.println("MQTT loop failed");
                }
            }
        }
        vTaskDelay(xDelay);
    }
}

void MQTT::reconnectMQTT(void) {
    if(!WiFi.isConnected()) {
        Serial.println("WiFi not connected, skip MQTT reconnect");
        return;
    }

    if(client.connected()) {
        return;
    }

    String clientId = "ESP32-" + WiFi.macAddress();
    int retries = 0;
    
    while (!client.connected() && retries < 3) {
        Serial.print("Attempting MQTT connection...");
        
        // Disconnect if there's any existing connection
        client.disconnect();
        
        // Set longer timeout
        secureClient.setTimeout(30);
        
        if (client.connect(clientId.c_str())) {
            Serial.println("MQTT connected");
            client.loop(); // Process any pending messages
            vTaskDelay(pdMS_TO_TICKS(500)); // Wait for connection to stabilize
            return;
        } else {
            Serial.printf("Failed, rc=%d ", client.state());
            switch(client.state()) {
                case -4: Serial.println("(MQTT_CONNECTION_TIMEOUT)"); break;
                case -3: Serial.println("(MQTT_CONNECTION_LOST)"); break;
                case -2: Serial.println("(MQTT_CONNECT_FAILED)"); break;
                case -1: Serial.println("(MQTT_DISCONNECTED)"); break;
            }
            retries++;
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    
    if (retries >= 3) {
        Serial.println("Failed to connect to MQTT after 3 attempts");
    }
}

void MQTT::mqttCallback(char *topic, byte *payload, unsigned int length) {
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);
  char body[1024];
  memcpy(body, payload, length);
  body[length] = '\0';
  Serial.print("Message: ");
  Serial.println(body);
  Serial.println("-----------------------");

  StaticJsonDocument<1024> jsonDocument;
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
  client.publish(publish_topic, message);
}

bool MQTT::subscribeTopic(const char* subscribe_topic) {
  if (client.subscribe(subscribe_topic)) {
    return true;
  }
  return false;
}