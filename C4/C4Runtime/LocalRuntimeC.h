#pragma once
// Local connections behind Bedrock's Net::LocalListener and Net::LocalStream (B67): a channel between one user's
// processes on one machine, found by name. Named pipes on Windows (Net/LocalPipesWin32.c). No other machine and no other
// user can use one: the pipe refuses remote clients, its only access entry is the current user, and a connection is
// refused when the listener runs as anyone else (a name another user claimed first). A pipe is an opaque pointer;
// counts, kNetTimedOut and kNetError are NetRuntimeC.h's, and so are the error texts.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct c4_local_pipe c4_local_pipe;

// Claims `name` and listens there; NULL with `error` when it is taken (someone listens there already) or not a name.
c4_local_pipe* c4_local_listen(const char* name, char* error, int64_t errorCapacity);
// The next client's connection; NULL with *timedOut set when none came within `timeoutMs` (a client that left before
// it was accepted counts as none), or NULL with `error`.
c4_local_pipe* c4_local_accept(c4_local_pipe* listener, int32_t timeoutMs, int32_t* timedOut, char* error, int64_t errorCapacity);
// A connection to the listener at `name`; NULL with `error` when no one listens there, or with *timedOut when it stayed
// busy for `timeoutMs`.
c4_local_pipe* c4_local_connect(const char* name, int32_t timeoutMs, int32_t* timedOut, char* error, int64_t errorCapacity);
// Bytes moved, 0 from a receive once the other side has closed, kNetTimedOut or kNetError. What was sent before a close
// can still be received.
int64_t c4_local_send(c4_local_pipe* pipe, const void* data, int64_t size, int32_t timeoutMs);
int64_t c4_local_receive(c4_local_pipe* pipe, void* buffer, int64_t capacity, int32_t timeoutMs);
void c4_local_close(c4_local_pipe* pipe);
// The current user's identity as text - the SID on Windows ("S-1-5-21-..."): its length, or -1.
int64_t c4_local_user_id(char* out, int64_t capacity);

#ifdef __cplusplus
}
#endif
