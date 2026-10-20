namespace Live
{
	class Widget
	{
	public:
		public int32 size = 1;
		public virtual int32 Size() const { return size; }
	}

	int32 g_unloads = 0;
	int32 g_finalized = 0;
	Widget* g_widget = nullptr;

	void Hold(void* object);
}
