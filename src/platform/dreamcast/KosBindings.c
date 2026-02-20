#include "KosBindings.h"
#include <string.h>

void NB_KOS_BindingsInit(void) {}

void NB_KOS_BindingsRead(NB_KOS_RawPadState* outState) {
    if (!outState) return;
    memset(outState, 0, sizeof(*outState));

    maple_device_t* dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (!dev) return;

    cont_state_t* st = (cont_state_t*)maple_dev_status(dev);
    if (!st) return;

    outState->has_controller = 1;
    outState->buttons = st->buttons;
    outState->stick_x = st->joyx;
    outState->stick_y = st->joyy;
    outState->l_trigger = st->ltrig;
    outState->r_trigger = st->rtrig;
}
