namespace Registry
{
	class Component
	{
	public:
		public int32 owner = 0;
		public virtual int32 Id() const { return 0; }
	}

	int32 g_unloads = 0;

	// [tunable("name")]: a field the host tunes, by name (a host header declares what its scripts write).
	annotation tunable(const char* name) on field;
}
