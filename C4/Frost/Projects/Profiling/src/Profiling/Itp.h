// Itp.h - the C and C++ client of the IED Trace Protocol (IED docs/16): a program run with IED_PROFILER set streams
// its zones, frames, plots, messages, thread names, tasks and allocations to the IED profiler (the Profile task sets
// it to "127.0.0.1:<port>;token=<t>"), or with "file:<path>" to a trace file. Without it every call costs one relaxed
// atomic load. Two files, Itp.h and Itp.cpp, with no dependency but the C++ standard library and the system's sockets:
// copy them into any C or C++ project.
//
//     void Update() {
//         ITP_ZONE("Update");                 // a zone to the end of the scope, with its function, file and line
//         itp_plot("Entities", (double)count);
//     }
//     ITP_FRAME();                            // once per presented frame
//
// Build with ITP_ENABLE=0 to compile the ITP_* macros away; the functions stay and cost one load.

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Where a zone is. A static one is registered with the profiler once per session; leave id and session zero.
typedef struct itp_site
{
	const char* name;
	const char* function;
	const char* file;
	uint32_t line;
	uint32_t color;
	uint64_t id;
	int64_t session;
} itp_site;

enum itp_severity { ITP_TRACE, ITP_DEBUG, ITP_INFO, ITP_WARNING, ITP_ERROR, ITP_FATAL };

// Starts recording to `target` ("host:port;token=<t>" or "file:<path>") as `app`; 0 on success, else -1 with why in
// `problem` (when given). A program run by IED does not call it: its first ITP call starts from IED_PROFILER.
int itp_start(const char* target, const char* app, char* problem, size_t problem_size);
// Sends what is buffered and ends the session (also done at exit).
void itp_stop(void);
int itp_active(void);

void itp_zone_begin(itp_site* site);
// A zone whose name is given per call (a file being loaded): `name` is shown, `site` is where it is.
void itp_zone_begin_named(itp_site* site, const char* name, size_t length);
void itp_zone_end(void);
void itp_zone_text(const char* text, size_t length);
void itp_zone_value(uint64_t value);

// The end of a frame of the default set (`set` NULL) or of a named one; discontinuous frames.
void itp_frame(const char* set);
void itp_frame_begin(const char* set);
void itp_frame_end(const char* set);

void itp_plot(const char* name, double value);
void itp_message(int severity, const char* text, size_t length);
// Names the calling thread from now on; call it again whenever the thread takes another role.
void itp_thread_name(const char* name);

// Tasks: units of work grouped by name, which may suspend on one thread and resume on another. 0 when off.
uint64_t itp_task_begin(const char* name, uint64_t parent);
void itp_task_end(uint64_t task);
void itp_task_suspend(uint64_t task);
void itp_task_resume(uint64_t task);

// Memory of the program's pools (pool 0 is the default heap).
void itp_alloc(const void* address, size_t size, uint32_t pool);
void itp_free(const void* address, uint32_t pool);
void itp_pool_name(uint32_t pool, const char* name);
void itp_pool_reset(uint32_t pool);

#ifdef __cplusplus
}

#include <string_view>

namespace itp
{
	// A zone for a scope.
	class Zone
	{
	public:
		explicit Zone(itp_site* site) : open(itp_active() != 0) { if (open) itp_zone_begin(site); }
		Zone(itp_site* site, std::string_view name) : open(itp_active() != 0) { if (open) itp_zone_begin_named(site, name.data(), name.size()); }
		~Zone() { if (open) itp_zone_end(); }
		Zone(const Zone&) = delete;
		Zone& operator=(const Zone&) = delete;

		void Text(std::string_view text) { if (open) itp_zone_text(text.data(), text.size()); }
		void Value(uint64_t value) { if (open) itp_zone_value(value); }

	private:
		bool open;
	};

	// A task for a scope.
	class Task
	{
	public:
		explicit Task(const char* name, uint64_t parent = 0) : id(itp_task_begin(name, parent)) {}
		~Task() { itp_task_end(id); }
		Task(const Task&) = delete;
		Task& operator=(const Task&) = delete;

		uint64_t id;
	};
}
#endif

#define ITP_CONCAT_INNER(a, b) a##b
#define ITP_CONCAT(a, b) ITP_CONCAT_INNER(a, b)

#if !defined(ITP_ENABLE) || ITP_ENABLE
#define ITP_SITE(variable, name) static itp_site variable = { name, __FUNCTION__, __FILE__, (uint32_t)__LINE__, 0, 0, 0 }
#ifdef __cplusplus
#define ITP_ZONE(name) ITP_SITE(ITP_CONCAT(itpSite, __LINE__), name); itp::Zone ITP_CONCAT(itpZone, __LINE__)(&ITP_CONCAT(itpSite, __LINE__))
#define ITP_ZONE_FUNCTION() ITP_ZONE(__FUNCTION__)
#endif
#define ITP_ZONE_BEGIN(name) do { ITP_SITE(itpSite, name); itp_zone_begin(&itpSite); } while (0)
#define ITP_ZONE_END() itp_zone_end()
#define ITP_FRAME() itp_frame(NULL)
#define ITP_PLOT(name, value) itp_plot(name, (double)(value))
#define ITP_MESSAGE(text) itp_message(ITP_INFO, text, sizeof(text) - 1)
#define ITP_THREAD_NAME(name) itp_thread_name(name)
#else
#define ITP_ZONE(name)
#define ITP_ZONE_FUNCTION()
#define ITP_ZONE_BEGIN(name)
#define ITP_ZONE_END()
#define ITP_FRAME()
#define ITP_PLOT(name, value)
#define ITP_MESSAGE(text)
#define ITP_THREAD_NAME(name)
#endif
