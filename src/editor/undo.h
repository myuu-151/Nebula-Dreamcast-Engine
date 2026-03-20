#pragma once

#include <string>
#include <vector>
#include <functional>

struct UndoAction
{
    std::string label;
    std::function<void()> undo;
    std::function<void()> redo;
};

extern std::vector<UndoAction> gUndoStack;
extern std::vector<UndoAction> gRedoStack;

void PushUndo(const UndoAction& action);
void DoUndo();
void DoRedo();
