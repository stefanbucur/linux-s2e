/*
 * Input Multitouch Library
 *
 * Copyright (c) 2008-2010 Henrik Rydberg
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/input/mt.h>
#include <linux/export.h>
#include <linux/slab.h>

#define TRKID_SGN	((TRKID_MAX + 1) >> 1)

#define input_dev_to_mt(dev)						\
	((dev)->mt ? container_of((dev)->mt, struct input_mt, slots[0]) : NULL)

static void copy_abs(struct input_dev *dev, unsigned int dst, unsigned int src)
{
	if (dev->absinfo && test_bit(src, dev->absbit)) {
		dev->absinfo[dst] = dev->absinfo[src];
		dev->absbit[BIT_WORD(dst)] |= BIT_MASK(dst);
	}
}

/**
 * input_mt_init_slots() - initialize MT input slots
 * @dev: input device supporting MT events and finger tracking
 * @num_slots: number of slots used by the device
 *
 * This function allocates all necessary memory for MT slot handling
 * in the input device, prepares the ABS_MT_SLOT and
 * ABS_MT_TRACKING_ID events for use and sets up appropriate buffers.
 * May be called repeatedly. Returns -EINVAL if attempting to
 * reinitialize with a different number of slots.
 */
int input_mt_init_slots_flags(struct input_dev *dev, unsigned int num_slots,
			unsigned int flags)
{
	struct input_mt *mt;
	int i;

	if (!num_slots)
		return 0;
	if (dev->mt)
		return dev->mtsize != num_slots ? -EINVAL : 0;

	mt = kzalloc(sizeof(*mt) + num_slots * sizeof(*mt->slots) +
		     num_slots * sizeof(*mt->slot_frame), GFP_KERNEL);
	if (!mt)
		goto err_mem;

	dev->mtsize = num_slots;
	mt->num_slots = num_slots;
	mt->flags = flags;
	mt->slot_frame = (unsigned int *)&mt->slots[num_slots];
	input_set_abs_params(dev, ABS_MT_SLOT, 0, num_slots - 1, 0, 0);
	input_set_abs_params(dev, ABS_MT_TRACKING_ID, 0, TRKID_MAX, 0, 0);
	input_set_events_per_packet(dev, 6 * num_slots);

	if (flags & (INPUT_MT_POINTER | INPUT_MT_DIRECT)) {
		__set_bit(EV_KEY, dev->evbit);
		__set_bit(BTN_TOUCH, dev->keybit);

		copy_abs(dev, ABS_X, ABS_MT_POSITION_X);
		copy_abs(dev, ABS_Y, ABS_MT_POSITION_Y);
		copy_abs(dev, ABS_PRESSURE, ABS_MT_PRESSURE);
	}
	if (flags & INPUT_MT_POINTER) {
		__set_bit(BTN_TOOL_FINGER, dev->keybit);
		__set_bit(BTN_TOOL_DOUBLETAP, dev->keybit);
		if (num_slots >= 3)
			__set_bit(BTN_TOOL_TRIPLETAP, dev->keybit);
		if (num_slots >= 4)
			__set_bit(BTN_TOOL_QUADTAP, dev->keybit);
		if (num_slots >= 5)
			__set_bit(BTN_TOOL_QUINTTAP, dev->keybit);
		__set_bit(INPUT_PROP_POINTER, dev->propbit);
	}
	if (flags & INPUT_MT_DIRECT)
		__set_bit(INPUT_PROP_DIRECT, dev->propbit);
	if (flags & INPUT_MT_TRACK) {
		unsigned int n2 = num_slots * num_slots;
		mt->red = kcalloc(n2, sizeof(*mt->red), GFP_KERNEL);
		if (!mt->red)
			goto err_mem;
	}

	/* Mark slots as 'unused' */
	for (i = 0; i < num_slots; i++)
		input_mt_set_value(&mt->slots[i], ABS_MT_TRACKING_ID, -1);

	dev->mt = &mt->slots[0];
	return 0;
err_mem:
	kfree(mt);
	return -ENOMEM;
}
EXPORT_SYMBOL(input_mt_init_slots_flags);

int input_mt_init_slots(struct input_dev *dev, unsigned int num_slots)
{
	return input_mt_init_slots_flags(dev, num_slots, 0);
}
EXPORT_SYMBOL(input_mt_init_slots);

/**
 * input_mt_destroy_slots() - frees the MT slots of the input device
 * @dev: input device with allocated MT slots
 *
 * This function is only needed in error path as the input core will
 * automatically free the MT slots when the device is destroyed.
 */
void input_mt_destroy_slots(struct input_dev *dev)
{
	if (dev->mt) {
		struct input_mt *mt = input_dev_to_mt(dev);
		kfree(mt->red);
		kfree(mt);
	}
	dev->mt = NULL;
	dev->mtsize = 0;
	dev->slot = 0;
	dev->trkid = 0;
}
EXPORT_SYMBOL(input_mt_destroy_slots);

/**
 * input_mt_report_slot_state() - report contact state
 * @dev: input device with allocated MT slots
 * @tool_type: the tool type to use in this slot
 * @active: true if contact is active, false otherwise
 *
 * Reports a contact via ABS_MT_TRACKING_ID, and optionally
 * ABS_MT_TOOL_TYPE. If active is true and the slot is currently
 * inactive, or if the tool type is changed, a new tracking id is
 * assigned to the slot. The tool type is only reported if the
 * corresponding absbit field is set.
 */
void input_mt_report_slot_state(struct input_dev *dev,
				unsigned int tool_type, bool active)
{
	struct input_mt *mt = input_dev_to_mt(dev);
	struct input_mt_slot *slot;
	int id;

	if (!mt)
		return;

	slot = &mt->slots[dev->slot];
	mt->slot_frame[dev->slot] = mt->frame;

	if (!active) {
		input_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
		return;
	}

	id = input_mt_get_value(slot, ABS_MT_TRACKING_ID);
	if (id < 0 || input_mt_get_value(slot, ABS_MT_TOOL_TYPE) != tool_type)
		id = input_mt_new_trkid(dev);

	input_event(dev, EV_ABS, ABS_MT_TRACKING_ID, id);
	input_event(dev, EV_ABS, ABS_MT_TOOL_TYPE, tool_type);
}
EXPORT_SYMBOL(input_mt_report_slot_state);

/**
 * input_mt_report_finger_count() - report contact count
 * @dev: input device with allocated MT slots
 * @count: the number of contacts
 *
 * Reports the contact count via BTN_TOOL_FINGER, BTN_TOOL_DOUBLETAP,
 * BTN_TOOL_TRIPLETAP and BTN_TOOL_QUADTAP.
 *
 * The input core ensures only the KEY events already setup for
 * this device will produce output.
 */
void input_mt_report_finger_count(struct input_dev *dev, int count)
{
	input_event(dev, EV_KEY, BTN_TOOL_FINGER, count == 1);
	input_event(dev, EV_KEY, BTN_TOOL_DOUBLETAP, count == 2);
	input_event(dev, EV_KEY, BTN_TOOL_TRIPLETAP, count == 3);
	input_event(dev, EV_KEY, BTN_TOOL_QUADTAP, count == 4);
	input_event(dev, EV_KEY, BTN_TOOL_QUINTTAP, count == 5);
}
EXPORT_SYMBOL(input_mt_report_finger_count);

/**
 * input_mt_report_pointer_emulation() - common pointer emulation
 * @dev: input device with allocated MT slots
 * @use_count: report number of active contacts as finger count
 *
 * Performs legacy pointer emulation via BTN_TOUCH, ABS_X, ABS_Y and
 * ABS_PRESSURE. Touchpad finger count is emulated if use_count is true.
 *
 * The input core ensures only the KEY and ABS axes already setup for
 * this device will produce output.
 */
void input_mt_report_pointer_emulation(struct input_dev *dev, bool use_count)
{
	struct input_mt_slot *oldest = 0;
	int oldid = dev->trkid;
	int count = 0;
	int i;

	for (i = 0; i < dev->mtsize; ++i) {
		struct input_mt_slot *ps = &dev->mt[i];
		int id = input_mt_get_value(ps, ABS_MT_TRACKING_ID);

		if (id < 0)
			continue;
		if ((id - oldid) & TRKID_SGN) {
			oldest = ps;
			oldid = id;
		}
		count++;
	}

	input_event(dev, EV_KEY, BTN_TOUCH, count > 0);
	if (use_count)
		input_mt_report_finger_count(dev, count);

	if (oldest) {
		int x = input_mt_get_value(oldest, ABS_MT_POSITION_X);
		int y = input_mt_get_value(oldest, ABS_MT_POSITION_Y);
		int p = input_mt_get_value(oldest, ABS_MT_PRESSURE);

		input_event(dev, EV_ABS, ABS_X, x);
		input_event(dev, EV_ABS, ABS_Y, y);
		input_event(dev, EV_ABS, ABS_PRESSURE, p);
	} else {
		input_event(dev, EV_ABS, ABS_PRESSURE, 0);
	}
}
EXPORT_SYMBOL(input_mt_report_pointer_emulation);

/**
 * input_mt_sync_frame() - synchronize mt frame
 * @dev: input device with allocated MT slots
 *
 * Close the frame and prepare the internal state for a new one.
 * Depending on the flags, marks unused slots as inactive and performs
 * pointer emulation.
 */
void input_mt_sync_frame(struct input_dev *dev)
{
	struct input_mt *mt = input_dev_to_mt(dev);
	unsigned int i;

	if (!mt)
		return;

	if (mt->flags & INPUT_MT_DROP_UNUSED) {
		for (i = 0; i < mt->num_slots; i++) {
			if (mt->slot_frame[i] == mt->frame)
				continue;
			input_mt_slot(dev, i);
			input_event(dev, EV_ABS, ABS_MT_TRACKING_ID, -1);
		}
	}

	input_mt_report_pointer_emulation(dev, (mt->flags & INPUT_MT_POINTER));

	mt->frame++;
}
EXPORT_SYMBOL(input_mt_sync_frame);

static int adjust_dual(int *begin, int step, int *end, int eq)
{
	int f, *p, s, c;

	if (begin == end)
		return 0;

	f = *begin;
	p = begin + step;
	s = p == end ? f + 1 : *p;

	for (; p != end; p += step)
		if (*p < f)
			s = f, f = *p;
		else if (*p < s)
			s = *p;

	c = (f + s + 1) / 2;
	if (c == 0 || (c > 0 && !eq))
		return 0;
	if (s < 0)
		c *= 2;

	for (p = begin; p != end; p += step)
		*p -= c;

	return (c < s && s <= 0) || (f >= 0 && f < c);
}

static void find_reduced_matrix(int *w, int nr, int nc, int nrc)
{
	int i, k, sum;

	for (k = 0; k < nrc; k++) {
		for (i = 0; i < nr; i++)
			adjust_dual(w + i, nr, w + i + nrc, nr <= nc);
		sum = 0;
		for (i = 0; i < nrc; i += nr)
			sum += adjust_dual(w + i, 1, w + i + nr, nc <= nr);
		if (!sum)
			break;
	}
}

static int input_mt_set_matrix(struct input_mt *mt,
			       const struct input_mt_pos *pos, int num_pos)
{
	const struct input_mt_pos *p;
	struct input_mt_slot *s;
	int *w = mt->red;
	int x, y;

	for (s = mt->slots; s != mt->slots + mt->num_slots; s++) {
		if (!input_mt_is_active(s))
			continue;
		x = input_mt_get_value(s, ABS_MT_POSITION_X);
		y = input_mt_get_value(s, ABS_MT_POSITION_Y);
		for (p = pos; p != pos + num_pos; p++) {
			int dx = x - p->x, dy = y - p->y;
			*w++ = dx * dx + dy * dy;
		}
	}

	return w - mt->red;
}

static void input_mt_set_slots(struct input_mt *mt,
			       int *slots, int num_pos)
{
	struct input_mt_slot *s;
	int *w = mt->red, *p;

	for (p = slots; p != slots + num_pos; p++)
		*p = -1;

	for (s = mt->slots; s != mt->slots + mt->num_slots; s++) {
		if (!input_mt_is_active(s))
			continue;
		for (p = slots; p != slots + num_pos; p++)
			if (*w++ < 0)
				*p = s - mt->slots;
	}

	for (s = mt->slots; s != mt->slots + mt->num_slots; s++) {
		if (input_mt_is_active(s))
			continue;
		for (p = slots; p != slots + num_pos; p++)
			if (*p < 0) {
				*p = s - mt->slots;
				break;
			}
	}
}

/**
 * input_mt_assign_slots() - perform a best-match assignment
 * @dev: input device with allocated MT slots
 * @slots: the slot assignment to be filled
 * @pos: the position array to match
 * @num_pos: number of positions
 *
 * Performs a best match against the current contacts and returns
 * the slot assignment list. New contacts are assigned to unused
 * slots.
 *
 * Returns zero on success, or negative error in case of failure.
 */
int input_mt_assign_slots(struct input_dev *dev, int *slots,
			  const struct input_mt_pos *pos, int num_pos)
{
	struct input_mt *mt = input_dev_to_mt(dev);
	int nrc;

	if (!mt || !mt->red)
		return -ENXIO;
	if (num_pos > mt->num_slots)
		return -EINVAL;
	if (num_pos < 1)
		return 0;

	nrc = input_mt_set_matrix(mt, pos, num_pos);
	find_reduced_matrix(mt->red, num_pos, nrc / num_pos, nrc);
	input_mt_set_slots(mt, slots, num_pos);

	return 0;
}
EXPORT_SYMBOL(input_mt_assign_slots);
