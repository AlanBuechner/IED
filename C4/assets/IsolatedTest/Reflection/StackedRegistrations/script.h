// Stand-ins for script images that register the same identities: Script::Enemy (annotated with its version),
// the variable Script::g_value and the function Script::Tick, under `tag`, as one image. Versions 1 to 3.
// ShadowHostType registers Host::Thing (which the program registered) with no fields under `tag`.
unsigned int ScriptRegisterUnder(const char* tag, int version);
int* ScriptValueOf(int version);
void ShadowHostType(const char* tag);
