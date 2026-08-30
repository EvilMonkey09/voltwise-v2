#pragma once
#include "telemetry.h"

void pzemInit();
void pzemTask(void* param);
bool pzemResetEnergy(int address);
