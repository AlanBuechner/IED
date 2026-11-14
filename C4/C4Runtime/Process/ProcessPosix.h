#pragma once
// What the POSIX process files share: the program, arguments and environment a child starts with
// (CommandLinePosix.c), the pipes to it (PipesPosix.c), the processes a kill ends (TreeLinux.c), and the child's state
// behind c4_process::process (ProcessPosix.c, UsagePosix.c).

#include "ProcessRuntimeC.h"
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/resource.h>
#include <sys/types.h>

// A started child. It is reaped only when it is closed: until then the zombie it leaves when it ends keeps its id, and
// its process group's, from going to another process while a kill may still name them.
typedef struct c4_process_posix_child
{
	pid_t id;
	int pidfd;           // readable once the child ends; -1 on kernels without pidfd_open (before 5.3)
	bool leadsSession;   // started in a session of its own: a kill ends its process group and session too
	bool killOnClose;
	bool killed;         // ended by c4_process_kill, so its exit code is 1, as on Windows
} c4_process_posix_child;

// A descriptor kept in a handle field as fd + 1, so null is "none" even for descriptor 0.
static inline void* c4_process_posix_handle(int fd) { return fd < 0 ? NULL : (void*)(intptr_t)(fd + 1); }
static inline int c4_process_posix_fd(const void* handle) { return handle ? (int)((intptr_t)handle - 1) : -1; }

// --- CommandLinePosix.c ----------------------------------------------------------------------------------------------

// The program's path (malloc'd), absolute: a path as it is, a bare name looked for in PATH as execvp does. Null with
// `error` when there is none.
char* c4_process_posix_program(const char* program, char* error, int64_t errorCapacity);
// The system's shell (malloc'd): /bin/sh. Null with `error` when it is missing.
char* c4_process_posix_shell(char* error, int64_t errorCapacity);
// The child's argv (malloc'd; the strings are the caller's): the program as named and the arguments, or "sh -c" and
// the command. Null when out of memory.
char** c4_process_posix_arguments(const c4_process_options* start);
// The child's environment (malloc'd; the strings are this process's or the caller's). Null with `*failed` for an
// entry that names nothing, or out of memory.
char** c4_process_posix_environment(const c4_process_options* start, bool* failed);
// `path` made absolute against the working directory (malloc'd); null when out of memory or the folder is gone.
char* c4_process_posix_absolute(const char* path);

// --- PipesPosix.c ----------------------------------------------------------------------------------------------------

// When a wait of `timeoutMs` ends, in milliseconds on the monotonic clock; -1 for never.
int64_t c4_process_posix_deadline(int32_t timeoutMs);
// Milliseconds left until `deadline`, at least 0; -1 for no deadline (poll's "forever").
int32_t c4_process_posix_remaining(int64_t deadline);

// `fd` moved to 3 or above, close-on-exec: 0 to 2 are the standard streams a child is given. -1 when it cannot be
// (`fd` is closed either way).
int c4_process_posix_raise(int fd);
// A pipe whose parent end (`*parent`, non-blocking) reads when `parentReads`, else writes. Both ends close-on-exec and
// numbered 3 or above.
bool c4_process_posix_pipe(bool parentReads, int* parent, int* child);
// Closes the descriptor in a handle field and empties it.
void c4_process_posix_close(void** handle);

// --- ProcessPosix.c --------------------------------------------------------------------------------------------------

// Whether the child has ended, leaving it unreaped: its status into `info` and what it used in all into `usage` (each
// may be null). False while it runs (errno 0) or when it cannot be asked (errno says why).
bool c4_process_posix_ended(const c4_process_posix_child* child, siginfo_t* info, struct rusage* usage);

// --- TreeLinux.c -----------------------------------------------------------------------------------------------------

// Ends process `root` and every process below it, and with `session` its process group and session: `root` leads
// both. True unless `root` could not be signalled.
bool c4_process_posix_kill_tree(pid_t root, bool session);
