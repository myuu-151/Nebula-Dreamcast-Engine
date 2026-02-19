#include "nebula_dreamcast.h"

namespace NebulaDreamcast
{
    bool IsDreamcastSdkEnabled()
    {
    #ifdef NEBULA_DREAMCAST
        return true;
    #else
        return false;
    #endif
    }
}
