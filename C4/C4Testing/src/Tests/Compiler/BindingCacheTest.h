#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// The interop pass's cached bindings (Interop/InteropPass.cpp): one per header and way of parsing it.
	CLASS(GROUP = Testing) BindingCacheTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::BindingCacheTest);

		// BUG-445: two programs binding one header with different include folders evicted each other's binding, so CBind
		// ran over the D3D12 and Win32 SDK headers on every C4LSP analysis (2.8 s of each on Claymore's engine).
		TEST_FUNC(Compiler.BindingCache)
		void EachWayOfParsingAHeaderKeepsItsBinding();

		// BUG-451: a binding was kept while the bound header's own stamp held, after a header it includes had changed.
		TEST_FUNC(Compiler.BindingCache)
		void AHeaderTheBoundOneIncludesChangesTheBinding();
	};
} // namespace Testing
