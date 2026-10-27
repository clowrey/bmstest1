/*
 * Copyright (c) 2018, Angelo Haller
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Modified by jonny5532, 2025
 */

#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Lock-free SPSC ring buffer. Capacity must be a power of two.

struct ringbuf {
	_Atomic size_t read_idx;
	_Atomic size_t write_idx;
	size_t capacity_mask;
	uint8_t *data;
};

static inline void
ringbuf_init(struct ringbuf *rb, uint8_t *buffer, size_t capacity)
{
	rb->data = buffer;
	rb->capacity_mask = capacity - 1;
	atomic_init(&rb->read_idx, 0);
	atomic_init(&rb->write_idx, 0);
}

static inline size_t
_ringbuf_min(size_t a, size_t b)
{
	return a < b ? a : b;
}

/*!
 * \brief Write to ring buffer. Partial writes are not allowed.
 * \warning Only call this function from a single producer thread.
 *
 * \param rb Ring buffer instance.
 * \param buf Buffer holding data to be written to ring buffer.
 * \param buf_size Buffer size in bytes.
 * \return buf_size if the data was written, or 0 if there wasn't room for all
 * of it.
 */
static inline size_t
ringbuf_write(struct ringbuf *rb, const uint8_t *buf, size_t buf_size)
{
	size_t write_idx = atomic_load_explicit(&rb->write_idx, memory_order_relaxed);
	size_t read_idx = atomic_load_explicit(&rb->read_idx, memory_order_acquire);
	size_t capacity = rb->capacity_mask + 1;

	if (buf_size > capacity - (write_idx - read_idx))
		return 0;

	size_t offset = write_idx & rb->capacity_mask;
	size_t write0 = _ringbuf_min(buf_size, capacity - offset);

	memcpy(&rb->data[offset], buf, write0);
	memcpy(rb->data, buf + write0, buf_size - write0);

	atomic_store_explicit(&rb->write_idx, write_idx + buf_size, memory_order_release);
	return buf_size;
}

/*!
 * \brief Read from ring buffer.
 * \warning Only call this function from a single consumer thread.
 *
 * \param rb Ring buffer instance.
 * \param buf Buffer to copy data to from ring buffer, or NULL to just discard
 * the data.
 * \param buf_size Buffer size in bytes.
 * \return Number of bytes read from ring buffer.
 */
static inline size_t
ringbuf_read(struct ringbuf *rb, uint8_t *buf, size_t buf_size)
{
	size_t read_idx = atomic_load_explicit(&rb->read_idx, memory_order_relaxed);
	size_t write_idx = atomic_load_explicit(&rb->write_idx, memory_order_acquire);
	size_t read = _ringbuf_min(write_idx - read_idx, buf_size);

	if (buf) {
		size_t offset = read_idx & rb->capacity_mask;
		size_t read0 = _ringbuf_min(read, rb->capacity_mask + 1 - offset);

		memcpy(buf, &rb->data[offset], read0);
		memcpy(buf + read0, rb->data, read - read0);
	}

	atomic_store_explicit(&rb->read_idx, read_idx + read, memory_order_release);
	return read;
}

/*!
 * \brief Get a pointer to the next unread data, without consuming it.
 * \warning Only call this function from a single consumer thread.
 *
 * \param rb Ring buffer instance.
 * \param buf Set to point at the next unread byte in the ring buffer.
 * \return Number of bytes available contiguously from *buf, which may be less
 * than the total available if the data wraps around the end of the buffer.
 */
static inline size_t
ringbuf_peek(struct ringbuf *rb, uint8_t **buf)
{
	size_t read_idx = atomic_load_explicit(&rb->read_idx, memory_order_relaxed);
	size_t write_idx = atomic_load_explicit(&rb->write_idx, memory_order_acquire);
	size_t offset = read_idx & rb->capacity_mask;

	*buf = &rb->data[offset];
	return _ringbuf_min(write_idx - read_idx, rb->capacity_mask + 1 - offset);
}

/*!
 * \brief Discard data from the ring buffer, typically after ringbuf_peek().
 * \warning Only call this function from a single consumer thread.
 *
 * \param rb Ring buffer instance.
 * \param n Number of bytes to discard. This isn't checked, so it must not
 * exceed the number of unread bytes, e.g. the count ringbuf_peek() returned.
 */
static inline void
ringbuf_consume(struct ringbuf *rb, size_t n)
{
	size_t read_idx = atomic_load_explicit(&rb->read_idx, memory_order_relaxed);

	atomic_store_explicit(&rb->read_idx, read_idx + n, memory_order_release);
}

static inline size_t
ringbuf_free_space(struct ringbuf *rb)
{
	size_t write_idx = atomic_load_explicit(&rb->write_idx, memory_order_relaxed);
	size_t read_idx = atomic_load_explicit(&rb->read_idx, memory_order_acquire);
	size_t size = write_idx - read_idx;

	// Clamp, in case this isn't called from the producer thread and the two
	// loads were torn.
	if (size > rb->capacity_mask)
		return 0;

	return rb->capacity_mask + 1 - size;
}

#endif
