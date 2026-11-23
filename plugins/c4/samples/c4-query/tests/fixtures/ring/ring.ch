namespace Game
{
	class Ring
	{
		public int32 count;
		public static int32 Capacity() { return 8; }
		public void Push(int32 value) { count = count + value; }
	}
}
