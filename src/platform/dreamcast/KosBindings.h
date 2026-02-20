#pragma once

#include <dc/maple.h>
#include <dc/maple/controller.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NB_KOS_RawPadState {
    int has_controller;
    uint32_t buttons;
    int8_t stick_x;
    int8_t stick_y;
    uint8_t l_trigger;
    uint8_t r_trigger;
} NB_KOS_RawPadState;

void NB_KOS_BindingsInit(void);
void NB_KOS_BindingsRead(NB_KOS_RawPadState* outState);

#ifdef __cplusplus
}
#endif
