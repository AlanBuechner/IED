// Bedrock: the C4 default library. `include "bedrock.ch"` gives all of it except the math library
// (`include "math.ch"`). Its names live in one-level namespaces (D-58):
//   Core           containers (Array, List, Queue, Stack, HashMap), text (StringView, String, StringBuilder,
//                  Format), hashing and sorting
//   FS             files, paths and directories
//   Serialization  the DOM, JSON and YAML read and written, and the binary form of reflected objects
//   Reflection     the runtime reflection registry
//   GC             the garbage collector
//   Threading      threads, atomics, locks, futures and the job system
//   Platform       virtual memory, the machine and the process, loaded libraries, wall-clock time
// Namespaces are open: a program may add its own members to any of them, as long as names do not collide.

include "Container/initlist.ch"
include "Container/hash.ch"
include "Container/sort.ch"
include "Container/array.ch"
include "Container/queue.ch"
include "Container/stack.ch"
include "Container/list.ch"
include "Container/hashmap.ch"
include "Text/stringview.ch"
include "Text/string.ch"
include "Text/stringbuilder.ch"
include "Text/format.ch"
include "FS/file.ch"
include "FS/path.ch"
include "FS/directory.ch"
include "Reflection/reflect.ch"
include "Serialization/dom.ch"
include "Serialization/json.ch"
include "Serialization/jsonreader.ch"
include "Serialization/yaml.ch"
include "Serialization/yamlreader.ch"
include "Serialization/binary.ch"
include "Memory/gc.ch"
include "Threading/atomic.ch"
include "Threading/thread.ch"
include "Threading/mutex.ch"
include "Threading/condition.ch"
include "Threading/waitgroup.ch"
include "Threading/future.ch"
include "Threading/jobs.ch"
include "Platform/memory.ch"
include "Platform/system.ch"
include "Platform/library.ch"
include "Platform/time.ch"
