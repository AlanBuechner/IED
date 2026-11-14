// Virtual memory for Bedrock's Platform namespace (PlatformRuntimeC.h) over mmap, mprotect and munmap, with Windows'
// reserve and commit model kept by the runtime. A reservation is an inaccessible mapping (PROT_NONE): address space
// that holds no memory and is charged nothing. Committing pages makes them accessible (mprotect), which charges them to
// the system's commit accounting as Windows does; their memory comes on first touch, zeroed. Decommitting maps fresh
// inaccessible pages over them, which drops their memory and their charge, so they read as zero when committed again.
//
// Linux keeps no record of which inaccessible pages are reserved and which committed with no rights, nor where a
// reservation starts, so the runtime keeps one per reservation: its runs of pages that share state and protection.
// Memory the runtime did not reserve (the program's image, stacks, the C library's heap) is answered from
// /proc/self/maps: a mapping with no rights as reserved, any other as committed.

#include "PlatformRuntimeC.h"
#include "ModulesLinux.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

// The end of the user address space of x64 Linux (47-bit): addresses past it are the kernel's, unless a mapping on a
// 5-level-paging machine holds them.
static const uintptr_t kUserSpaceEnd = (uintptr_t)1 << 47;

enum { kFirstRuns = 4, kFirstReservations = 16 };

// A run of a reservation's pages sharing state and protection: from `page` (counted from the reservation's base) to
// the next run's.
typedef struct Run
{
	int64_t page;
	int32_t state;
	int32_t protection;
} Run;

typedef struct Reservation
{
	uintptr_t base;
	int64_t pages;
	Run* runs;          // by page, the first at 0; neighbours differ
	int32_t runCount;
} Reservation;

// The runtime's reservations by base, under `lock`.
typedef struct Space
{
	pthread_mutex_t lock;
	Reservation* reservations;
	int32_t count;
	int32_t capacity;
} Space;

static Space g_space = { PTHREAD_MUTEX_INITIALIZER, NULL, 0, 0 };

static int ToRights(int32_t protection)
{
	switch (protection)
	{
	case kProtectNone: return PROT_NONE;
	case kProtectRead: return PROT_READ;
	case kProtectReadWrite: return PROT_READ | PROT_WRITE;
	case kProtectExecute: return PROT_EXEC;
	case kProtectReadExecute: return PROT_READ | PROT_EXEC;
	case kProtectReadWriteExecute: return PROT_READ | PROT_WRITE | PROT_EXEC;
	default: return -1;
	}
}

// /proc/self/maps rights ("rwxp"); x64 has no write-only page, so writing implies reading.
static int32_t FromRights(const char* rights)
{
	const bool read = rights[0] == 'r';
	const bool write = rights[1] == 'w';
	const bool execute = rights[2] == 'x';
	if (write) return execute ? kProtectReadWriteExecute : kProtectReadWrite;
	if (execute) return read ? kProtectReadExecute : kProtectExecute;
	return read ? kProtectRead : kProtectNone;
}

static bool IsPowerOfTwo(int64_t value) { return value > 0 && (value & (value - 1)) == 0; }

static int64_t PageSize(void) { return (int64_t)sysconf(_SC_PAGESIZE); }

int64_t c4_vm_page_size(void) { return PageSize(); }

// mmap places a mapping at any page: reservations start at a multiple of the page size.
int64_t c4_vm_allocation_granularity(void) { return PageSize(); }

// --- the runtime's record of reservations ------------------------------------------------------------------------

// The index of the last reservation whose base is at or before `address`, -1 when none is.
static int32_t ReservationBefore(const Space* space, uintptr_t address)
{
	int32_t low = 0;
	int32_t high = space->count;
	while (low < high)
	{
		const int32_t middle = low + (high - low) / 2;
		if (space->reservations[middle].base <= address) low = middle + 1;
		else high = middle;
	}
	return low - 1;
}

static uintptr_t ReservationEnd(const Reservation* reservation)
{
	return reservation->base + (uintptr_t)(reservation->pages * PageSize());
}

// The reservation holding [address, address + size), or null when no one reservation holds it all.
static Reservation* ReservationHolding(Space* space, uintptr_t address, uintptr_t size)
{
	const int32_t index = ReservationBefore(space, address);
	if (index < 0) return NULL;
	Reservation* reservation = &space->reservations[index];
	const uintptr_t end = ReservationEnd(reservation);
	return address < end && size <= end - address ? reservation : NULL;
}

static bool Record(Space* space, uintptr_t base, int64_t pages, int32_t state, int32_t protection)
{
	if (space->count == space->capacity)
	{
		const int32_t capacity = space->capacity ? space->capacity * 2 : kFirstReservations;
		Reservation* grown = (Reservation*)realloc(space->reservations, (size_t)capacity * sizeof(Reservation));
		if (!grown) return false;
		space->reservations = grown;
		space->capacity = capacity;
	}
	Run* runs = (Run*)malloc(kFirstRuns * sizeof(Run));
	if (!runs) return false;
	runs[0] = (Run){ 0, state, protection };
	const int32_t at = ReservationBefore(space, base) + 1;
	memmove(&space->reservations[at + 1], &space->reservations[at], (size_t)(space->count - at) * sizeof(Reservation));
	space->reservations[at] = (Reservation){ base, pages, runs, 1 };
	space->count++;
	return true;
}

static void Forget(Space* space, Reservation* reservation)
{
	free(reservation->runs);
	const int32_t at = (int32_t)(reservation - space->reservations);
	memmove(&space->reservations[at], &space->reservations[at + 1], (size_t)(space->count - at - 1) * sizeof(Reservation));
	space->count--;
}

// The index of the run holding `page`.
static int32_t RunAt(const Reservation* reservation, int64_t page)
{
	int32_t low = 0;
	int32_t high = reservation->runCount;
	while (low < high)
	{
		const int32_t middle = low + (high - low) / 2;
		if (reservation->runs[middle].page <= page) low = middle + 1;
		else high = middle;
	}
	return low - 1;
}

static int64_t RunEnd(const Reservation* reservation, int32_t run)
{
	return run + 1 < reservation->runCount ? reservation->runs[run + 1].page : reservation->pages;
}

static void AppendRun(Run* runs, int32_t* count, Run run)
{
	const bool same = *count > 0 && runs[*count - 1].state == run.state && runs[*count - 1].protection == run.protection;
	if (!same) runs[(*count)++] = run;
}

// Pages [first, end) given `value`'s state and protection, the runs around them kept. False when out of memory.
static bool AssignRuns(Reservation* reservation, int64_t first, int64_t end, Run value)
{
	Run* runs = (Run*)malloc((size_t)(reservation->runCount + 2) * sizeof(Run));
	if (!runs) return false;
	int32_t count = 0;
	for (int32_t i = 0; i < reservation->runCount && reservation->runs[i].page < first; ++i) AppendRun(runs, &count, reservation->runs[i]);
	value.page = first;
	AppendRun(runs, &count, value);
	if (end < reservation->pages)
	{
		const int32_t after = RunAt(reservation, end);
		AppendRun(runs, &count, (Run){ end, reservation->runs[after].state, reservation->runs[after].protection });
		for (int32_t i = after + 1; i < reservation->runCount; ++i) AppendRun(runs, &count, reservation->runs[i]);
	}
	free(reservation->runs);
	reservation->runs = runs;
	reservation->runCount = count;
	return true;
}

static bool AllCommitted(const Reservation* reservation, int64_t first, int64_t end)
{
	for (int32_t run = RunAt(reservation, first); run < reservation->runCount && reservation->runs[run].page < end; ++run)
		if (reservation->runs[run].state != kRegionCommitted) return false;
	return true;
}

// --- reserve, commit and release ---------------------------------------------------------------------------

// A byte range rounded out to whole pages: [*first, *end) as page addresses. False for a size that overflows.
static bool PageRange(const void* address, int64_t size, uintptr_t* first, uintptr_t* end)
{
	const uintptr_t page = (uintptr_t)PageSize();
	const uintptr_t start = (uintptr_t)address;
	if (start > UINTPTR_MAX - page || (uint64_t)size > UINTPTR_MAX - page - start) return false;
	*first = start & ~(page - 1);
	*end = (start + (uintptr_t)size + page - 1) & ~(page - 1);
	return true;
}

static void* MapInaccessible(size_t bytes)
{
	void* mapped = mmap(NULL, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return mapped == MAP_FAILED ? NULL : mapped;
}

// More address space than asked for, then the parts before the first aligned address and after the size unmapped.
static void* MapAligned(size_t bytes, size_t alignment)
{
	const size_t page = (size_t)PageSize();
	if (bytes > SIZE_MAX - alignment) return NULL;
	uint8_t* mapped = (uint8_t*)MapInaccessible(bytes + alignment - page);
	if (!mapped) return NULL;
	uint8_t* aligned = (uint8_t*)(((uintptr_t)mapped + alignment - 1) & ~(uintptr_t)(alignment - 1));
	if (aligned > mapped) munmap(mapped, (size_t)(aligned - mapped));
	const size_t after = (size_t)((mapped + bytes + alignment - page) - (aligned + bytes));
	if (after > 0) munmap(aligned + bytes, after);
	return aligned;
}

static size_t ToPageBytes(int64_t size)
{
	const int64_t page = PageSize();
	return (size_t)((size + page - 1) / page * page);
}

static bool IsValidSize(int64_t size) { return size > 0 && size <= INT64_MAX - PageSize(); }

void* c4_vm_reserve(int64_t size, int64_t alignment)
{
	if (!IsValidSize(size) || alignment < 0 || (alignment != 0 && !IsPowerOfTwo(alignment))) return NULL;
	const size_t bytes = ToPageBytes(size);
	void* base = alignment > PageSize() ? MapAligned(bytes, (size_t)alignment) : MapInaccessible(bytes);
	if (!base) return NULL;
	pthread_mutex_lock(&g_space.lock);
	const bool recorded = Record(&g_space, (uintptr_t)base, (int64_t)(bytes / (size_t)PageSize()), kRegionReserved, kProtectNone);
	pthread_mutex_unlock(&g_space.lock);
	if (recorded) return base;
	munmap(base, bytes);
	return NULL;
}

void* c4_vm_allocate(int64_t size, int32_t protection)
{
	const int rights = ToRights(protection);
	if (!IsValidSize(size) || rights < 0) return NULL;
	const size_t bytes = ToPageBytes(size);
	void* base = mmap(NULL, bytes, rights, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (base == MAP_FAILED) return NULL;
	pthread_mutex_lock(&g_space.lock);
	const bool recorded = Record(&g_space, (uintptr_t)base, (int64_t)(bytes / (size_t)PageSize()), kRegionCommitted, protection);
	pthread_mutex_unlock(&g_space.lock);
	if (recorded) return base;
	munmap(base, bytes);
	return NULL;
}

// What the system does to pages: new rights for their memory, or fresh pages in place of it.
typedef bool (*PageChange)(void* address, size_t bytes, int rights);

static bool ProtectPages(void* address, size_t bytes, int rights) { return mprotect(address, bytes, rights) == 0; }

static bool FreshPages(void* address, size_t bytes, int rights)
{
	return mmap(address, bytes, rights, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED;
}

// The pages holding [address, address + size) given `state` and `protection`: the system's change, then the record's.
// -1 when they are not all in one reservation (or, `committedOnly`, not all committed), the system refuses, or the
// record cannot grow.
static int32_t ChangePages(void* address, int64_t size, int32_t state, int32_t protection, PageChange change, bool committedOnly)
{
	const int rights = ToRights(protection);
	uintptr_t first = 0;
	uintptr_t end = 0;
	if (!address || size <= 0 || rights < 0 || !PageRange(address, size, &first, &end)) return -1;
	pthread_mutex_lock(&g_space.lock);
	Reservation* reservation = ReservationHolding(&g_space, first, end - first);
	const int64_t page = PageSize();
	const int64_t firstPage = reservation ? (int64_t)(first - reservation->base) / page : 0;
	const int64_t endPage = reservation ? (int64_t)(end - reservation->base) / page : 0;
	bool changed = reservation && (!committedOnly || AllCommitted(reservation, firstPage, endPage)) &&
		change((void*)first, end - first, rights);
	if (changed) changed = AssignRuns(reservation, firstPage, endPage, (Run){ 0, state, protection });
	pthread_mutex_unlock(&g_space.lock);
	return changed ? 0 : -1;
}

int32_t c4_vm_commit(void* address, int64_t size, int32_t protection)
{
	return ChangePages(address, size, kRegionCommitted, protection, ProtectPages, false);
}

int32_t c4_vm_decommit(void* address, int64_t size)
{
	return ChangePages(address, size, kRegionReserved, kProtectNone, FreshPages, false);
}

int32_t c4_vm_protect(void* address, int64_t size, int32_t protection)
{
	return ChangePages(address, size, kRegionCommitted, protection, ProtectPages, true);
}

int32_t c4_vm_release(void* base)
{
	if (!base) return -1;
	pthread_mutex_lock(&g_space.lock);
	const int32_t index = ReservationBefore(&g_space, (uintptr_t)base);
	Reservation* reservation = index >= 0 && g_space.reservations[index].base == (uintptr_t)base ? &g_space.reservations[index] : NULL;
	const bool released = reservation && munmap(base, (size_t)(reservation->pages * PageSize())) == 0;
	if (released) Forget(&g_space, reservation);
	pthread_mutex_unlock(&g_space.lock);
	return released ? 0 : -1;
}

// --- query -------------------------------------------------------------------------------------------------

// As VirtualQuery answers: the region starts at the page holding the address and runs to the end of the pages that
// share its state and protection.
static void AnswerReservation(const Reservation* reservation, uintptr_t page, c4_memory_region* region)
{
	const int64_t pageBytes = PageSize();
	const int64_t at = (int64_t)(page - reservation->base) / pageBytes;
	const int32_t run = RunAt(reservation, at);
	region->base = (void*)page;
	region->size = (RunEnd(reservation, run) - at) * pageBytes;
	region->allocationBase = (void*)reservation->base;
	region->state = reservation->runs[run].state;
	region->protection = region->state == kRegionCommitted ? reservation->runs[run].protection : kProtectNone;
}

// What /proc/self/maps says of a page no reservation holds: the mapping holding it, or the gap it is in, either cut at
// the runtime's reservations around it (a reservation merges with a neighbouring mapping of the same rights). A file's
// mappings belong to the one at its start, as an image's sections belong to the image on Windows.
typedef struct Lookup
{
	uintptr_t page;
	uintptr_t low;          // the runtime's reservations end at or before `low` and start at or after `high`
	uintptr_t high;
	bool mapped;            // a mapping holds the page: [start, end) with `rights`
	uintptr_t start;        // else the page is in the gap [start, end)
	uintptr_t end;
	char rights[5];
	uintptr_t fileStart;    // the last mapping of a file's start (offset 0) before the page, and the file's inode
	uint64_t fileInode;
	uintptr_t allocation;   // what the page's mapping belongs to
} Lookup;

static bool FindPage(const c4_linux_mapping* mapping, void* state)
{
	Lookup* lookup = (Lookup*)state;
	if (mapping->inode != 0 && mapping->offset == 0)
	{
		lookup->fileStart = mapping->start;
		lookup->fileInode = mapping->inode;
	}
	if (mapping->end <= lookup->page)
	{
		lookup->start = mapping->end;
		return true;
	}
	lookup->mapped = mapping->start <= lookup->page;
	if (lookup->mapped) lookup->start = mapping->start;
	lookup->end = lookup->mapped ? mapping->end : mapping->start;
	const bool ofFile = mapping->inode != 0 && mapping->inode == lookup->fileInode;
	lookup->allocation = ofFile ? lookup->fileStart : mapping->start;
	memcpy(lookup->rights, mapping->rights, sizeof(lookup->rights));
	return false;
}

static uintptr_t Larger(uintptr_t a, uintptr_t b) { return a > b ? a : b; }
static uintptr_t Smaller(uintptr_t a, uintptr_t b) { return a < b ? a : b; }

static int32_t AnswerSystem(Lookup* lookup, c4_memory_region* region)
{
	if (!c4_linux_each_mapping(FindPage, lookup)) return -1;
	if (!lookup->mapped && lookup->page >= kUserSpaceEnd) return -1;
	const uintptr_t end =Smaller(lookup->mapped ? lookup->end : Smaller(lookup->end, kUserSpaceEnd), lookup->high);
	const int32_t rights = FromRights(lookup->rights);
	region->base = (void*)lookup->page;
	region->size = (int64_t)(end > lookup->page ? end - lookup->page : 0);
	region->allocationBase = lookup->mapped ? (void*)Larger(lookup->allocation, lookup->low) : NULL;
	region->state = !lookup->mapped ? kRegionFree : rights == kProtectNone ? kRegionReserved : kRegionCommitted;
	region->protection = region->state == kRegionCommitted ? rights : kProtectNone;
	return 0;
}

int32_t c4_vm_query(const void* address, c4_memory_region* region)
{
	if (!region) return -1;
	const uintptr_t page = (uintptr_t)address & ~(uintptr_t)(PageSize() - 1);
	Lookup lookup = { page, 0, UINTPTR_MAX, false, 0, UINTPTR_MAX, "----", 0, 0, 0 };
	pthread_mutex_lock(&g_space.lock);
	const int32_t index = ReservationBefore(&g_space, page);
	const Reservation* holding = index >= 0 && page < ReservationEnd(&g_space.reservations[index]) ? &g_space.reservations[index] : NULL;
	if (holding) AnswerReservation(holding, page, region);
	else
	{
		if (index >= 0) lookup.low = ReservationEnd(&g_space.reservations[index]);
		if (index + 1 < g_space.count) lookup.high = g_space.reservations[index + 1].base;
	}
	pthread_mutex_unlock(&g_space.lock);
	return holding ? 0 : AnswerSystem(&lookup, region);
}

// x64 keeps instruction fetch coherent with stores; the builtin says so for the compiler and does nothing here.
void c4_vm_flush_instruction_cache(const void* address, int64_t size)
{
	if (address && size > 0) __builtin___clear_cache((char*)address, (char*)address + size);
}
