#pragma once

// Draws the File, Edit, Tools, Package popups and the Preferences window.
// Call once per frame after the main menu bar items have been submitted.
void DrawMainMenus(bool& showPreferences, float& uiScale, int& themeMode);
