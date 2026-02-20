#pragma once
#include <dc/maple/controller.h>
#include <stdint.h>

#define NB_BTN_A CONT_A
#define NB_BTN_B CONT_B
#define NB_BTN_X CONT_X
#define NB_BTN_Y CONT_Y
#define NB_BTN_START CONT_START
#define NB_BTN_DPAD_UP CONT_DPAD_UP
#define NB_BTN_DPAD_DOWN CONT_DPAD_DOWN
#define NB_BTN_DPAD_LEFT CONT_DPAD_LEFT
#define NB_BTN_DPAD_RIGHT CONT_DPAD_RIGHT
#define NB_BTN_Z CONT_Z
#define NB_BTN_D CONT_D

void NB_KOS_InitInput(void);
void NB_KOS_PollInput(void);
int NB_KOS_ButtonDown(int buttonMask);
int NB_KOS_ButtonPressed(int buttonMask);
int NB_KOS_ButtonReleased(int buttonMask);
float NB_KOS_GetStickX(void);
float NB_KOS_GetStickY(void);
float NB_KOS_GetLTrigger(void);
float NB_KOS_GetRTrigger(void);
int NB_KOS_HasController(void);
uint32_t NB_KOS_GetRawButtons(void);
