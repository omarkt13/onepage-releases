/* OnePage Mini - input devices: touch strip, power button. */
#ifndef OP_INPUT_DEV_H
#define OP_INPUT_DEV_H

#include <stdbool.h>

/* Touch strip (IQS7222A). Posts EVT_INPUT with a ui_input. */
int touch_init(void);
bool touch_present(void);

/* Power button: short press, 3 s hold. 10 s forced restart is in hardware. */
int button_init(void);
/* Arms the button as a System OFF wake source. */
void button_prepare_poweroff(void);

#endif
