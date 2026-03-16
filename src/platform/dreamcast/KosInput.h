/*
 * KosInput.h — Nebula Dreamcast controller input wrappers (NB_KOS_*).
 *
 * Provides high-level input functions for gameplay scripts on Dreamcast.
 * Normalizes analog stick/trigger values, tracks per-frame button edges
 * (pressed/released), and abstracts away the raw Maple controller API.
 *
 * Usage:
 *   Call NB_KOS_InitInput() once at startup.
 *   Call NB_KOS_PollInput() once per frame before any button/stick queries.
 *   Use NB_KOS_ButtonDown/Pressed/Released for digital input.
 *   Use NB_KOS_GetStickX/Y and NB_KOS_GetLTrigger/RTrigger for analog.
 *
 * Button IDs (NB_BTN_*) map directly to KOS CONT_* constants.
 *
 * See docs/Dreamcast_Binding_API.md for full API documentation.
 */
#pragma once
#include <dc/maple/controller.h>
#include <stdint.h>

/* ---- Button ID mappings ---- */

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

/* ---- Input API ---- */

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
