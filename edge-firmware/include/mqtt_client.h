#pragma once

void mqttInit();
void mqttTask(void* param);
bool mqttIsConnected();
bool mqttIsConfigured();
