#include "gesture.h"

#include <string.h>

void gesture_init(struct gesture_rec *g)
{
	memset(g, 0, sizeof(*g));
}

static enum gesture emit(struct gesture_rec *g, uint32_t t, enum gesture e)
{
	g->t_last_event = t;
	g->have_event = true;
	return e;
}

static int16_t clamp_pos(int16_t p)
{
	if (p < 0) {
		return 0;
	}
	return p > GESTURE_POS_MAX ? GESTURE_POS_MAX : p;
}

enum gesture gesture_feed(struct gesture_rec *g, uint32_t t, bool touching, int16_t pos)
{
	if (touching) {
		pos = clamp_pos(pos);
		if (!g->down) {
			g->down = true;
			g->t_down = t;
			g->p_down = g->p_min = g->p_max = g->p_last = pos;
			g->ignore = g->have_event && (t - g->t_last_event) < GESTURE_REFRACTORY_MS;
			return GESTURE_NONE;
		}
		g->p_last = pos;
		if (pos < g->p_min) {
			g->p_min = pos;
		}
		if (pos > g->p_max) {
			g->p_max = pos;
		}
		if (!g->ignore && (t - g->t_down) >= GESTURE_LONG_MS &&
		    (g->p_max - g->p_min) <= GESTURE_TAP_SLOP) {
			g->ignore = true; /* the release must not add a tap */
			return emit(g, t, GESTURE_LONG_PRESS);
		}
		return GESTURE_NONE;
	}

	if (!g->down) {
		return GESTURE_NONE;
	}
	g->down = false;
	if (g->ignore) {
		return GESTURE_NONE;
	}
	uint32_t dur = t - g->t_down;
	int net = g->p_last - g->p_down;
	int spread = g->p_max - g->p_min;

	if (dur < GESTURE_MIN_MS) {
		return GESTURE_NONE;
	}
	if (spread <= GESTURE_TAP_SLOP) {
		return dur <= GESTURE_TAP_MAX_MS ? emit(g, t, GESTURE_TAP) : GESTURE_NONE;
	}
	if (dur <= GESTURE_MAX_SWIPE_MS) {
		/* Net travel must carry most of the spread, so a wiggle is rejected. */
		int anet = net < 0 ? -net : net;

		if (anet >= GESTURE_SWIPE_MIN && anet * 4 >= spread * 3) {
			return emit(g, t, net > 0 ? GESTURE_SWIPE_FWD : GESTURE_SWIPE_BACK);
		}
	}
	return GESTURE_NONE;
}
