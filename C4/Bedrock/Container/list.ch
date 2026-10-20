// List<T>: a doubly linked list (std::list). Inserting or removing never moves the other elements, so a
// ListNode<T>* stays valid until its own element is removed - the handle an LRU cache or an intrusive queue
// keeps. Each node is one allocation, registered with the GC so references held in it stay alive. A zero
// List is empty and allocates nothing.
//     Core::List<int32> order = { 1, 2 };
//     Core::ListNode<int32>* last = order.PushBack(3);
//     order.MoveToFront(last);                       // 3 1 2
//     for (int32 n : order) printf("%d\n", n);

include "initlist.ch"
include "storage.ch"

namespace Core
{
	template<T> struct ListNode {
		public T value;
		public ListNode<T>* next;       // null at the back
		public ListNode<T>* previous;   // null at the front
	}

	template<T> struct ListIterator {
		public ListNode<T>* node;
		[operator("notequal")] public bool NotEqual(const ListIterator<T>& other) const { return node != other.node; }
		[operator("dereference")] public T& Element() { return node->value; }
		[operator("increment")] public ListIterator<T> Next() { ListIterator<T> next; next.node = node->next; return next; }
	}

	template<T> struct ConstListIterator {
		public const ListNode<T>* node;
		[operator("notequal")] public bool NotEqual(const ConstListIterator<T>& other) const { return node != other.node; }
		[operator("dereference")] public const T& Element() const { return node->value; }
		[operator("increment")] public ConstListIterator<T> Next() { ConstListIterator<T> next; next.node = node->next; return next; }
	}

	template<T> class List
	{
	public:
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(InitList<T> items);
		[operator("copy")] public void CopyFrom(const List<T>& other);
		[operator("delete")] public void Delete() { Clear(); }

		public int64 Count() const { return count; }
		public bool IsEmpty() const { return count == 0; }

		public T& Front() { return head->value; }
		public const T& Front() const { return head->value; }
		public T& Back() { return tail->value; }
		public const T& Back() const { return tail->value; }
		public ListNode<T>* First() { return head; }   // null when empty
		public ListNode<T>* Last() { return tail; }
		public const ListNode<T>* First() const { return head; }
		public const ListNode<T>* Last() const { return tail; }

		// Each returns the new element's node.
		// Each copies `value` in, or moves it: PushBack(take(x)) puts in a value that cannot be copied.
		public ListNode<T>* PushFront(T value) { return InsertBefore(head, take(value)); }
		public ListNode<T>* PushBack(T value) { return InsertBefore(nullptr, take(value)); }
		public ListNode<T>* InsertBefore(ListNode<T>* position, T value);   // a null position is the back
		public ListNode<T>* InsertAfter(ListNode<T>* position, T value);    // a null position is the front

		public T PopFront();   // the front moved out; a zero T when empty
		public T PopBack();    // the back moved out; a zero T when empty
		public void Remove(ListNode<T>* node);   // `node` must be in this list
		public void Clear();

		// Relinks an element of this list; no element is copied or moved.
		public void MoveToFront(ListNode<T>* node);
		public void MoveToBack(ListNode<T>* node);
		public void Reverse();

		// The first node whose value == value, or null.
		public ListNode<T>* Find(const T& value);
		public const ListNode<T>* Find(const T& value) const;
		public bool Contains(const T& value) const { return Find(value) != nullptr; }

		[operator("begin")] public ListIterator<T> Begin() { ListIterator<T> it; it.node = head; return it; }
		[operator("end")] public ListIterator<T> End() { ListIterator<T> it; return it; }
		[operator("begin")] public ConstListIterator<T> Begin() const { ConstListIterator<T> it; it.node = head; return it; }
		[operator("end")] public ConstListIterator<T> End() const { ConstListIterator<T> it; return it; }

	private:
		private ListNode<T>* NewNode(T value);
		private void FreeNode(ListNode<T>* node);
		private void Link(ListNode<T>* node, ListNode<T>* before);   // before == null links at the back
		private void Unlink(ListNode<T>* node);

		private ListNode<T>* head;
		private ListNode<T>* tail;
		private int64 count;
	}
}
