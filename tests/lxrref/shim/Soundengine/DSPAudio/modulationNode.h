#pragma once
#include <stdint.h>
typedef struct { uint16_t destination; uint8_t type; float amount; float lastVal; } ModulationNode;
extern ModulationNode velocityModulators[6];
void modNode_init(ModulationNode *vm);
void modNode_updateValue(ModulationNode *vm, float val);
