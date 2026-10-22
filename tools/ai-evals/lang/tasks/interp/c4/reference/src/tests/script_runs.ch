include "interpreter.ch"
include "parser.ch"

// What a script printed, and its error ("line N: message"), empty when it ran through.
struct ScriptRun
{
	public Core::String output;
	public Core::String error;
}

ScriptRun RunSource(const char* source) {
	Calc::BufferOutput buffer;
	ScriptRun run;
	try {
		Calc::RunScript(Core::StringView(source), &buffer);
	} catch (const Calc::ScriptError& error) {
		run.error = error.Describe();
	}
	run.output = buffer.text.ToString();
	return run;
}

void ExpectOutput(const char* source, const char* output) {
	ScriptRun run = RunSource(source);
	assert_se("", run.error.CStr(), source);
	assert_se(output, run.output.CStr(), source);
}

void ExpectError(const char* source, const char* output, const char* error) {
	ScriptRun run = RunSource(source);
	assert_se(output, run.output.CStr(), source);
	assert_se(error, run.error.CStr(), source);
}

// The error `source` fails to parse with, empty when it parses.
Core::String ParseErrorOf(const char* source) {
	try {
		Calc::Parse(Core::StringView(source));
	} catch (const Calc::ScriptError& error) {
		return error.Describe();
	}
	return Core::String();
}
