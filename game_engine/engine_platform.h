#ifndef ENGINE_PLATFORM_H
#define ENGINE_PLATFORM_H

#include <stdint.h>
#include "engine.h"

void engine_platform_init(EngineInput *input);
void engine_platform_update_input(EngineInput *input);
uint32_t engine_platform_millis(void);
u8g2_t *engine_platform_display(void);
uint8_t engine_platform_is_paused(void);
void engine_platform_set_paused(uint8_t paused);
void engine_platform_toggle_pause(void);

#endif
