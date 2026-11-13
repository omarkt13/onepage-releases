#include "clock.h"

#include <zephyr/kernel.h>

static struct {
	bool valid;
	uint32_t local_at_set;
	int64_t uptime_at_set;
} clk;

static struct k_spinlock clk_lock;

void clock_set_local(uint32_t local_seconds)
{
	k_spinlock_key_t key = k_spin_lock(&clk_lock);

	clk.local_at_set = local_seconds;
	clk.uptime_at_set = k_uptime_get();
	clk.valid = true;
	k_spin_unlock(&clk_lock, key);
}

bool clock_valid(void)
{
	return clk.valid;
}

uint32_t clock_now(void)
{
	k_spinlock_key_t key = k_spin_lock(&clk_lock);
	uint32_t now = 0;

	if (clk.valid) {
		now = clk.local_at_set + (uint32_t)((k_uptime_get() - clk.uptime_at_set) / 1000);
	}
	k_spin_unlock(&clk_lock, key);
	return now;
}

uint64_t clock_uptime_ms(void)
{
	return (uint64_t)k_uptime_get();
}
