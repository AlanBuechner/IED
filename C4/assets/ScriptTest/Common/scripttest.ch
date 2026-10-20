// What every ScriptTest host shares (assets/ScriptTest/RunTests.ps1): its checks, and compiling its scripts from
// scripts/ beside it at the run's optimization (`host.exe O2` compiles them -O2).
include "bedrock.ch"
include "scripting.ch"

namespace ScriptTest
{
	// The checks of one run: each one that fails prints a line, and the host returns how many did.
	class Checks
	{
	public:
		public int32 failures = 0;
		public bool optimize = false;

		public void That(bool condition, const char* what)
		{
			if (condition) return;
			printf("FAIL: %s\n", what);
			failures++;
		}

		public void Equal(int64 expected, int64 actual, const char* what)
		{
			if (expected == actual) return;
			printf("FAIL: %s: expected %lld, got %lld\n", what, expected, actual);
			failures++;
		}

		public void Contains(const char* text, const char* part, const char* what)
		{
			if (text != nullptr && Core::StringView(text).Contains(Core::StringView(part))) return;
			printf("FAIL: %s: no '%s' in:\n%s\n", what, part, text == nullptr ? "(null)" : text);
			failures++;
		}

		// The run's result: 0 when every check passed.
		public int32 End(const char* suite)
		{
			if (failures == 0) printf("%s: OK\n", suite);
			else printf("%s: %d FAILED\n", suite, failures);
			return failures;
		}
	}

	bool SameText(const char* a, const char* b) { return strcmp((void*)a, (void*)b) == 0; }

	Checks Begin(int32 argc, char** argv)
	{
		Checks checks;
		checks.optimize = argc > 1 && SameText(argv[1], "O2");
		return checks;
	}

	// The program's script host; a failed check when it does not open.
	Scripting::Host* OpenHost(Checks& checks)
	{
		Scripting::Host* host = Scripting::Host::Open();
		if (!host->IsOpen()) printf("host: %s\n", host->Error());
		checks.That(host->IsOpen(), "the host opens");
		return host;
	}

	// Options named `name` at the run's optimization, with the files of scripts/ `files` names (separated by ';').
	Scripting::ScriptOptions OptionsFor(const Checks& checks, const char* name, const char* files)
	{
		Scripting::ScriptOptions options;
		options.name = Core::String(name);
		options.optimize = checks.optimize;
		Core::StringView all(files);
		for (int64 start = 0; start < all.ByteLength(); )
		{
			int64 end = all.IndexOf(';', start);
			if (end < 0) end = all.ByteLength();
			Core::String path("scripts/");
			path.Append(all.Substring(start, end - start));
			options.AddFile(path.CStr());
			start = end + 1;
		}
		return options;
	}

	// The script compiled and loaded from `options`; its diagnostics print when it did not load.
	Scripting::Script* Load(Scripting::Host* host, Checks& checks, const Scripting::ScriptOptions& options)
	{
		Scripting::Script* script = host->Compile(options);
		if (!script->Ok()) printf("script '%s':\n%s%s\n", options.name.CStr(), script->Diagnostics(), script->FaultText());
		checks.That(script->Ok(), "the script loads");
		return script;
	}
}
