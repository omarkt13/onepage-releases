/* OnePage Mini - wall clock set from the iPhone's Current Time Service. */
#ifndef OP_CLOCK_H
#define OP_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

void clock_set_local(uint32_t local_seconds);
bool clock_valid(void);
/* Local seconds now, or 0 if the clock was never set. */
uint32_t clock_now(void);
/* Milliseconds since boot, counting through sleep. */
uint64_t clock_uptime_ms(void);

#endif
