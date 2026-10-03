#pragma once
#include <Arduino.h>

void localMqttInit();
void localMqttHandle();
void localMqttPublish();
void localMqttPublishAttributes();
bool localMqttIsConnected();

void     localMqttSetBroker(const String& ip, uint16_t port);
String   localMqttGetBrokerIP();
uint16_t localMqttGetBrokerPort();
