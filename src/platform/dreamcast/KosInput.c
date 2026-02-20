#include "KosInput.h"
#include "KosBindings.h"

static NB_KOS_RawPadState gPrevState;
static NB_KOS_RawPadState gCurrState;

static float NB_NormalizeStick(int8_t v) {
    float f = (float)v / 127.0f;
    if (f < -1.0f) return -1.0f;
    if (f > 1.0f) return 1.0f;
    return f;
}

static float NB_NormalizeTrigger(uint8_t v) {
    return (float)v / 255.0f;
}

void NB_KOS_InitInput(void) {
    NB_KOS_BindingsInit();
    gPrevState.has_controller = 0;
    gPrevState.buttons = 0;
    gPrevState.stick_x = 0;
    gPrevState.stick_y = 0;
    gPrevState.l_trigger = 0;
    gPrevState.r_trigger = 0;
    gCurrState = gPrevState;
    NB_KOS_BindingsRead(&gCurrState);
    gPrevState = gCurrState;
}

void NB_KOS_PollInput(void) {
    gPrevState = gCurrState;
    NB_KOS_BindingsRead(&gCurrState);
}

int NB_KOS_ButtonDown(int buttonMask) {
    return (gCurrState.buttons & (uint32_t)buttonMask) != 0;
}

int NB_KOS_ButtonPressed(int buttonMask) {
    uint32_t mask = (uint32_t)buttonMask;
    return (gCurrState.buttons & mask) != 0 && (gPrevState.buttons & mask) == 0;
}

int NB_KOS_ButtonReleased(int buttonMask) {
    uint32_t mask = (uint32_t)buttonMask;
    return (gCurrState.buttons & mask) == 0 && (gPrevState.buttons & mask) != 0;
}

float NB_KOS_GetStickX(void) {
    return NB_NormalizeStick(gCurrState.stick_x);
}

float NB_KOS_GetStickY(void) {
    return NB_NormalizeStick(gCurrState.stick_y);
}

float NB_KOS_GetLTrigger(void) {
    return NB_NormalizeTrigger(gCurrState.l_trigger);
}

float NB_KOS_GetRTrigger(void) {
    return NB_NormalizeTrigger(gCurrState.r_trigger);
}

int NB_KOS_HasController(void) {
    return gCurrState.has_controller;
}

uint32_t NB_KOS_GetRawButtons(void) {
    return gCurrState.buttons;
}
