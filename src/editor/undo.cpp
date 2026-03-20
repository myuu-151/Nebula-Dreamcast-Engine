#include "undo.h"
#include "../viewport/viewport_transform.h"

std::vector<UndoAction> gUndoStack;
std::vector<UndoAction> gRedoStack;

void PushUndo(const UndoAction& action)
{
    gUndoStack.push_back(action);
    gRedoStack.clear();
}

void DoUndo()
{
    // If a transform is active or pending, finalize it so Ctrl+Z undoes the transform first.
    if (gTransformMode != Transform_None || gHasTransformSnapshot)
    {
        EndTransformSnapshot();
        gTransformMode = Transform_None;
        gAxisLock = 0;
    }

    if (gUndoStack.empty()) return;
    UndoAction action = gUndoStack.back();
    gUndoStack.pop_back();
    if (action.undo) action.undo();
    gRedoStack.push_back(action);
}

void DoRedo()
{
    // If a transform is active or pending, finalize it so redo targets last transform first.
    if (gTransformMode != Transform_None || gHasTransformSnapshot)
    {
        EndTransformSnapshot();
        gTransformMode = Transform_None;
        gAxisLock = 0;
    }

    if (gRedoStack.empty()) return;
    UndoAction action = gRedoStack.back();
    gRedoStack.pop_back();
    if (action.redo) action.redo();
    gUndoStack.push_back(action);
}
