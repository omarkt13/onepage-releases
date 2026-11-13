/*
 * OnePage Mini - strip gestures from IQS7222A slider samples.
 * Position is normalised to 0..1000 along the strip.
 *
 * Accidental-touch rules:
 *  - contacts shorter than GESTURE_MIN_MS are ignored (brush, flex)
 *  - a contact that wanders more than a tap but travels less than a swipe
 *    is ignored (grip, palm)
 *  - contacts longer than GESTURE_MAX_SWIPE_MS that move are ignored (pocket)
 *  - a new contact within GESTURE_REFRACTORY_MS of the last event is ignored
 */
#ifndef OP_GESTURE_H
#define OP_GESTURE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GESTURE_POS_MAX 1000
#define GESTURE_MIN_MS 40u
#define GESTURE_TAP_MAX_MS 350u
#define GESTURE_LONG_MS 600u
#define GESTURE_MAX_SWIPE_MS 1000u
#define GESTURE_TAP_SLOP 60
#define GESTURE_SWIPE_MIN 150
#define GESTURE_REFRACTORY_MS 150u

enum gesture {
	GESTURE_NONE = 0,
	GESTURE_TAP,
	GESTURE_LONG_PRESS,
	GESTURE_SWIPE_FWD,  /* position increasing */
	GESTURE_SWIPE_BACK, /* position decreasing */
};

struct gesture_rec {
	bool down;
	bool ignore;       /* current contact rejected or already used */
	uint32_t t_down;
	int16_t p_down;
	int16_t p_min;
	int16_t p_max;
	int16_t p_last;
	uint32_t t_last_event;
	bool have_event;
};

void gesture_init(struct gesture_rec *g);
/*
 * Feeds one sample. touching=false means no contact. Returns an event, if
 * this sample completes one. Call at least every 50 ms while touching so a
 * long press is reported while the finger is still down.
 */
enum gesture gesture_feed(struct gesture_rec *g, uint32_t t_ms, bool touching, int16_t pos);

#ifdef __cplusplus
}
#endif

#endif /* OP_GESTURE_H */
