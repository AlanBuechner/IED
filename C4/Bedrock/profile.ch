// The profiler client: `include "profile.ch"` (bedrock.ch does not include it). Everything is in `namespace Profile`
// (Profile/profile.ch): zones, frames, plots, messages, thread names, tasks, locks and memory, and GPU zones
// (Profile/gpu.ch), streamed to the IED profiler when the program runs with IED_PROFILER set. The PROFILE_* macros
// below name a zone's source file and line; build with -D PROFILE=0 (a configuration's defines in nuke.toml) and they
// compile to nothing - all but PROFILE_MUTEX, which stays a Profile::Mutex (one load a lock and unlock when no session
// runs).

include "Profile/profile.ch"
include "Profile/gpu.ch"

#define PROFILE_JOIN_INNER(a, b) a##b
#define PROFILE_JOIN(a, b) PROFILE_JOIN_INNER(a, b)
#define PROFILE_MUTEX(variable, name) Profile::Mutex variable(name, __FILE__, __LINE__)

#if !defined(PROFILE) || PROFILE
#define PROFILE_ZONE(name) static Profile::Site PROFILE_JOIN(profileSite_, __LINE__)(name, __FILE__, __LINE__); Profile::Zone PROFILE_JOIN(profileZone_, __LINE__)(PROFILE_JOIN(profileSite_, __LINE__))
#define PROFILE_FRAME() Profile::Frame()
#define PROFILE_PLOT(name, value) Profile::Plot(Core::StringView(name), (double)(value))
#define PROFILE_MESSAGE(text) Profile::Message(Core::StringView(text), Profile::Severity::Info)
#define PROFILE_THREAD_NAME(name) Profile::SetThreadName(Core::StringView(name))
#else
#define PROFILE_ZONE(name)
#define PROFILE_FRAME()
#define PROFILE_PLOT(name, value)
#define PROFILE_MESSAGE(text)
#define PROFILE_THREAD_NAME(name)
#endif
