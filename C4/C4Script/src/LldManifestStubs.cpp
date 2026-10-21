// lld's COFF driver can merge Windows manifests, which takes libxml2: the vendored LLVM is built with it but does not
// ship the library. Debug scripts (docs/runtime/04 section 6.4) are linked with /manifest:no, so the merger never runs;
// these stand in for libxml2 at link time and stop the process should it ever be called.
#include <cstdio>
#include <cstdlib>

namespace
{
	[[noreturn]] void ManifestMergerCalled()
	{
		std::fputs("C4Script: lld tried to merge a manifest, which this build cannot do (it has no libxml2)\n", stderr);
		std::abort();
	}
}

extern "C"
{
	void* xmlFree = nullptr; // libxml2's is a variable holding its free function
	void xmlSetGenericErrorFunc() { ManifestMergerCalled(); }
	void xmlReadMemory() { ManifestMergerCalled(); }
	void xmlDocGetRootElement() { ManifestMergerCalled(); }
	void xmlFreeDoc() { ManifestMergerCalled(); }
	void xmlUnlinkNode() { ManifestMergerCalled(); }
	void xmlFreeNode() { ManifestMergerCalled(); }
	void xmlNewProp() { ManifestMergerCalled(); }
	void xmlStrdup() { ManifestMergerCalled(); }
	void xmlCopyNamespace() { ManifestMergerCalled(); }
	void xmlAddChild() { ManifestMergerCalled(); }
	void xmlNewDoc() { ManifestMergerCalled(); }
	void xmlDocSetRootElement() { ManifestMergerCalled(); }
	void xmlDocDumpFormatMemoryEnc() { ManifestMergerCalled(); }
	void xmlFreeNs() { ManifestMergerCalled(); }
	void xmlNewNs() { ManifestMergerCalled(); }
}
