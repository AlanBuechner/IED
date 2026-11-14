// Threads for the program and Bedrock (D-48, ThreadRuntimeC.h): what every system shares. A thread started here
// attaches before its entry runs. The system's part (starting, joining, waiting on an address, processors, names) is
// ThreadRuntimeWin32.c or ThreadRuntimePosix.c.

#include "GCRuntimeC.h"
#include "PlatformRuntimeC.h"
#include "ThreadRuntimeC.h"
#include "ThreadStart.h"
#include <stdlib.h>

void __c4_thread_start(void); // the program's thread_local starts (CodeGen/CodeGenDriver.cpp)
void c4_install_terminate(void); // EHRuntime.cpp: an uncaught exception ends the process cleanly on this thread too

static _Thread_local int t_attached;

void c4_thread_attach(void)
{
	if (t_attached) return;
	t_attached = 1;
#ifdef __linux__
	c4_crash_attach_thread(); // a stack of its own for the fault handler, so a stack overflow here is reported
#endif
	c4_install_terminate();
	c4_gc_register_thread();
	__c4_thread_start();
}

typedef void (*c4_thread_entry)(void* state);

typedef struct StartPacket
{
	c4_thread_entry entry;
	void* state;
} StartPacket;

void* c4_thread_packet(void* entry, void* state)
{
	StartPacket* packet = (StartPacket*)malloc(sizeof(StartPacket));
	if (!packet) return NULL;
	packet->entry = (c4_thread_entry)entry;
	packet->state = state;
	c4_gc_pin(state);
	return packet;
}

void c4_thread_drop_packet(void* packet)
{
	c4_gc_unpin(((StartPacket*)packet)->state);
	free(packet);
}

void c4_thread_run_packet(void* parameter)
{
	StartPacket packet = *(StartPacket*)parameter;
	free(parameter);
	c4_thread_attach();
	c4_gc_unpin(packet.state); // this thread's stack holds it now
	packet.entry(packet.state);
}
