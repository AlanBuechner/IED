#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// What C4LSP puts on the wire: positions in the encoding the client and the server agreed on (L1, Protocol/
	// PositionCodec), and request ids echoed as the client sent them (L2).
	CLASS(GROUP = Testing) LspWireTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspWireTest);

		TEST_FUNC(Lsp.Wire)
		void TestLengthsCountUtf16UnitsAndUtf8Bytes();

		TEST_FUNC(Lsp.Wire)
		void TestNegotiationPrefersUtf8WhenOffered();

		TEST_FUNC(Lsp.Wire)
		void TestMessagesAreRewrittenByTheNearestUri();

		TEST_FUNC(Lsp.Wire)
		void TestServerAnswersInTheNegotiatedEncoding();

		TEST_FUNC(Lsp.Wire)
		void TestStringRequestIdIsEchoed();
	};
} // namespace Testing
