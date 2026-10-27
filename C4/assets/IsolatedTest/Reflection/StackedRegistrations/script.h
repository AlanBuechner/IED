// Stand-ins for script images that register the same identities: Script::Enemy (annotated with its version, and
// with flags and a start of its own: version 1 starts as 1, 2 holds pointers and has no start, 3 starts as 3),
// the variable Script::g_value and the function Script::Tick, under `tag`, as one image. Versions 1 to 3.
// ShadowHostType registers Host::Thing (which the program registered) with no fields under `tag`.
unsigned int ScriptRegisterUnder(const char* tag, int version);
int* ScriptValueOf(int version);
void ShadowHostType(const char* tag);
