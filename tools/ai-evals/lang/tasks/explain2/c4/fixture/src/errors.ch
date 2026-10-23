include "bedrock.ch"

namespace Calc
{
	// A problem in a script - a lex, parse or runtime error - and the line it is on. It is thrown, and caught
	// where the script is run.
	class ScriptError
	{
		public int64 line;
		public Core::String message;

		[operator("create")] public void Init(int64 line, Core::String message) {
			this.line = line;
			this.message = take(message);
		}

		// "line 3: division by zero"
		public Core::String Describe() const {
			Core::StringBuilder text;
			text.Append("line ");
			text.AppendInt(line);
			text.Append(": ");
			text.Append(message.View());
			return text.ToString();
		}
	}
}
