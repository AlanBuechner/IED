#pragma once
// How a system's thread file (ThreadRuntimeWin32.c, ThreadRuntimePosix.c) starts a thread: it makes a packet of the
// entry and its state (the state pinned until the thread holds it), runs it on the new thread, or drops it when no
// thread started.

void* c4_thread_packet(void* entry, void* state);
void c4_thread_drop_packet(void* packet);
// Attaches the calling thread and runs the packet's entry; frees the packet.
void c4_thread_run_packet(void* packet);
