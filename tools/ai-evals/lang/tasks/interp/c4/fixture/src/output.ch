include "bedrock.ch"

namespace Calc
{
	// Where a script's print writes.
	interface IOutput
	{
		void Write(Core::StringView text);
	}

	class ConsoleOutput : IOutput
	{
		public void Write(Core::StringView text) { Platform::WriteOutput(text); }
	}

	// Keeps what is written, for tests.
	class BufferOutput : IOutput
	{
		public Core::StringBuilder text;
		public void Write(Core::StringView text) { this.text.Append(text); }
	}
}
