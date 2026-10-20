namespace Pool
{
	class Node
	{
	public:
		public int32 value = 0;
		public Node* next = nullptr;
		public virtual int32 Weight() const { return 1; }
	}

	int32 g_finalized = 0;
	Core::Array<Node*> g_kept;

	void Keep(Node* node);
	// Allocates and drops `count` host objects, so collections have garbage to find.
	void Churn(int32 count);
}
