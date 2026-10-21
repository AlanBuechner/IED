// Buffers that grow (GCRuntimeC.h, c4_buffer_resize): heap memory while small, reserved address space once large, so a
// large buffer grows by committing the pages after it instead of copying itself into a bigger block. A scanned buffer
// is a collector scan region: the collector may stop the program between any two instructions, so the region never
// covers memory that is given back, and the elements stay in a registered region while they move.

#include "GCRuntimeC.h"
#include "PlatformRuntimeC.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

enum { kLargeBufferBytes = 1 << 20 };
// A reservation holds this many times the size it is made for, and at least kSmallestReservation: a buffer that keeps
// doubling copies itself once every three doublings past that.
enum { kReservationGrowth = 8 };
static const size_t kSmallestReservation = (size_t)64 << 20;

static bool IsLarge(size_t bytes) { return bytes >= kLargeBufferBytes; }

static size_t ToPages(size_t bytes)
{
	const size_t page = (size_t)c4_vm_page_size();
	return (bytes + page - 1) / page * page;
}

static size_t ReservationFor(size_t bytes)
{
	const size_t grown = bytes <= SIZE_MAX / kReservationGrowth ? bytes * kReservationGrowth : bytes;
	return grown > kSmallestReservation ? grown : kSmallestReservation;
}

// Room for `bytes` and growth after them, the first `bytes` committed; exactly `bytes` when address space is short.
static void* ReserveLarge(size_t bytes)
{
	void* base = c4_vm_reserve((int64_t)ReservationFor(bytes), 0);
	if (!base) base = c4_vm_reserve((int64_t)bytes, 0);
	if (!base) return NULL;
	if (c4_vm_commit(base, (int64_t)bytes, kProtectReadWrite) == 0) return base;
	c4_vm_release(base);
	return NULL;
}

static void* Allocate(size_t bytes) { return IsLarge(bytes) ? ReserveLarge(bytes) : malloc(bytes); }

static void Release(void* buffer, size_t bytes)
{
	if (!buffer) return;
	if (IsLarge(bytes)) c4_vm_release(buffer);
	else free(buffer);
}

// The reserved bytes right after a large buffer's `committed` ones: 0 when its reservation ends there.
static size_t ReservedAfter(void* buffer, size_t committed)
{
	c4_memory_region region;
	if (c4_vm_query((char*)buffer + committed, &region) != 0) return 0;
	if (region.state != kRegionReserved || region.allocationBase != buffer) return 0;
	return (size_t)region.size;
}

static void Register(bool scanned, void* buffer, size_t bytes)
{
	if (scanned) c4_gc_register_region(buffer, bytes);
}

// A large buffer resized in place: the pages after it committed, or its tail given back. False when its reservation
// has no room for the growth.
static bool ResizeInPlace(void* buffer, size_t oldBytes, size_t newBytes, bool scanned)
{
	const size_t committed = ToPages(oldBytes);
	const size_t needed = ToPages(newBytes);
	if (needed > committed)
	{
		if (needed - committed > ReservedAfter(buffer, committed)) return false;
		if (c4_vm_commit((char*)buffer + committed, (int64_t)(needed - committed), kProtectReadWrite) != 0) return false;
	}
	Register(scanned, buffer, newBytes);
	if (needed < committed) c4_vm_decommit((char*)buffer + needed, (int64_t)(committed - needed));
	return true;
}

// The kept bytes copied into a new block, which is registered before they arrive and the old one unregistered before
// it goes.
static void* Move(void* buffer, size_t oldBytes, size_t keepBytes, size_t newBytes, bool scanned)
{
	void* moved = Allocate(newBytes);
	if (!moved) return NULL;
	Register(scanned, moved, newBytes);
	if (keepBytes) memcpy(moved, buffer, keepBytes);
	if (scanned && buffer) c4_gc_unregister_region(buffer);
	Release(buffer, oldBytes);
	return moved;
}

static void Free(void* buffer, size_t bytes, bool scanned)
{
	if (scanned && buffer) c4_gc_unregister_region(buffer);
	Release(buffer, bytes);
}

static void* Resize(void* buffer, size_t oldBytes, size_t keepBytes, size_t newBytes, bool scanned)
{
	if (!buffer) oldBytes = 0;
	if (keepBytes > oldBytes) keepBytes = oldBytes;
	if (keepBytes > newBytes) keepBytes = newBytes;
	if (newBytes == 0)
	{
		Free(buffer, oldBytes, scanned);
		return NULL;
	}
	if (IsLarge(oldBytes) && IsLarge(newBytes) && ResizeInPlace(buffer, oldBytes, newBytes, scanned)) return buffer;
	return Move(buffer, oldBytes, keepBytes, newBytes, scanned);
}

void* c4_buffer_resize(void* buffer, size_t oldBytes, size_t keepBytes, size_t newBytes)
{
	return Resize(buffer, oldBytes, keepBytes, newBytes, false);
}

void c4_buffer_free(void* buffer, size_t bytes) { Free(buffer, bytes, false); }

void* c4_buffer_resize_scanned(void* buffer, size_t oldBytes, size_t keepBytes, size_t newBytes)
{
	return Resize(buffer, oldBytes, keepBytes, newBytes, true);
}

void c4_buffer_free_scanned(void* buffer, size_t bytes) { Free(buffer, bytes, true); }
