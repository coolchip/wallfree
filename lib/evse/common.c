/* SPDX-License-Identifier: Apache-2.0 */
#include <wallfree/evse/common.h>

void evse_debounce_init(struct evse_debounce *d, int value)
{
	d->value = value;
	d->dev_since = EVSE_MS_NONE;
}

bool evse_debounce_update(struct evse_debounce *d, int candidate, evse_ms_t now,
			  evse_ms_t hold)
{
	if (candidate == d->value) {
		d->dev_since = EVSE_MS_NONE;
		return false;
	}
	if (d->dev_since == EVSE_MS_NONE) {
		d->dev_since = now;
		return false;
	}
	if (evse_elapsed_gt(now, d->dev_since, hold)) {
		d->value = candidate;
		d->dev_since = EVSE_MS_NONE;
		return true;
	}
	return false;
}
