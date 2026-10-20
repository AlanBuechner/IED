#include "VectorTest.h"

// ─── Test Data Types ────────────────────────────────────────────────────────

/// Simple struct for testing Vector with user-defined types.
struct TestStruct
{
	int32 x;
	int32 y;

	TestStruct() : x(0), y(0) {}
	TestStruct(int32 inX, int32 inY) : x(inX), y(inY) {}

	bool operator==(const TestStruct& other) const { return x == other.x && y == other.y; }
	bool operator!=(const TestStruct& other) const { return !(*this == other); }
};

/// Type that tracks how many move, copy, and destructor calls occur.
struct TrackedType
{
	static int32 MoveCount;
	static int32 CopyCount;
	static int32 DestroyCount;

	int32 value;

	TrackedType() : value(0) {}
	explicit TrackedType(int32 inValue) : value(inValue) {}

	TrackedType(const TrackedType& other) : value(other.value) { ++CopyCount; }
	TrackedType(TrackedType&& other) noexcept : value(other.value) { other.value = 0; ++MoveCount; }

	TrackedType& operator=(const TrackedType& other)
	{
		if (this != &other) { value = other.value; ++CopyCount; }
		return *this;
	}

	TrackedType& operator=(TrackedType&& other) noexcept
	{
		if (this != &other) { value = other.value; other.value = 0; ++MoveCount; }
		return *this;
	}

	~TrackedType() { ++DestroyCount; }

	bool operator==(const TrackedType& other) const { return value == other.value; }

	static void ResetCounters() { MoveCount = 0; CopyCount = 0; DestroyCount = 0; }
};

int32 TrackedType::MoveCount = 0;
int32 TrackedType::CopyCount = 0;
int32 TrackedType::DestroyCount = 0;

/// Base class for inheritance hierarchy tests.
struct Base
{
	int32 baseValue;

	Base() : baseValue(0) {}
	explicit Base(int32 inValue) : baseValue(inValue) {}
	virtual ~Base() = default;

	virtual bool operator==(const Base& other) const { return baseValue == other.baseValue; }
};

/// Derived class extending Base.
struct Derived : public Base
{
	int32 derivedValue;

	Derived() : Base(), derivedValue(0) {}
	Derived(int32 inBase, int32 inDerived) : Base(inBase), derivedValue(inDerived) {}

	virtual ~Derived() = default;

	bool operator==(const Derived& other) const
	{
		return Base::operator==(other) && derivedValue == other.derivedValue;
	}
};

/// Type with a custom comparison function for use with Vector predicates.
struct CustomCompare
{
	int32 id;

	CustomCompare() : id(0) {}
	explicit CustomCompare(int32 inId) : id(inId) {}

	bool operator==(const CustomCompare& other) const { return id == other.id; }
	bool operator!=(const CustomCompare& other) const { return !(*this == other); }

	/// Custom comparator callable for use with FindIf / Contains-style predicates.
	struct Comparator
	{
		bool operator()(const CustomCompare& lhs, const CustomCompare& rhs) const
		{
			return lhs.id == rhs.id;
		}
	};
};

namespace Testing
{
	// ─── Constructor Tests ──────────────────────────────────────────────────

	void VectorTest::TestDefaultConstructor()
	{
		Core::Vector<int32> vec;

		if (vec.Count() != 0)
		{
			uint32 got = vec.Count();
			TEST_FAILED("Default constructor: expected count 0, got {0}", got);
		}

		if (!vec.Empty())
			TEST_FAILED("Default constructor: expected Empty() to be true");

		if (vec.Capacity() != 0)
		{
			uint32 got = vec.Capacity();
			TEST_FAILED("Default constructor: expected capacity 0, got {0}", got);
		}

		TEST_PASSED();
	}

	void VectorTest::TestCopyConstructor()
	{
		Core::Vector<int32> original{10, 20, 30};
		Core::Vector<int32> copy(original);

		// Verify copy has same values
		if (copy.Count() != 3)
			TEST_FAILED("Copy constructor: expected count 3, got {0}", copy.Count());

		if (copy[0] != 10 || copy[1] != 20 || copy[2] != 30)
			TEST_FAILED("Copy constructor: values mismatch");

		// Verify independent copy — modifying original does not affect copy
		original[0] = 99;
		if (copy[0] != 10)
			TEST_FAILED("Copy constructor: copy not independent, copy[0] is {0}, expected 10", copy[0]);

		// Copy should have its own data pointer (not shallow copy)
		if (original.Data() == copy.Data())
			TEST_FAILED("Copy constructor: data pointers are the same (shallow copy detected)");

		TEST_PASSED();
	}

	void VectorTest::TestMoveConstructor()
	{
		Core::Vector<int32> original{10, 20, 30};

		Core::Vector<int32> moved(std::move(original));

		// Verify moved vector has the values
		if (moved.Count() != 3)
			TEST_FAILED("Move constructor: expected count 3, got {0}", moved.Count());

		if (moved[0] != 10 || moved[1] != 20 || moved[2] != 30)
			TEST_FAILED("Move constructor: values mismatch in moved vector");

		// Verify original is left empty after move
		if (original.Count() != 0)
			TEST_FAILED("Move constructor: expected original count 0, got {0}", original.Count());

		if (!original.Empty())
			TEST_FAILED("Move constructor: expected original to be empty after move");

		// Original should still be in a valid state (empty vector is valid)
		Core::Vector<int32> another{1, 2};
		another = std::move(original);
		if (another.Count() != 0)
			TEST_FAILED("Move constructor: moved-from original was not in valid state");

		TEST_PASSED();
	}

	void VectorTest::TestCountConstructor()
	{
		Core::Vector<int32> vec(5);

		if (vec.Count() != 5)
			TEST_FAILED("Count constructor: expected count 5, got {0}", vec.Count());

		// All elements should be value-initialized (0 for int32)
		for (uint32 i = 0; i < vec.Count(); i++)
		{
			if (vec[i] != 0)
				TEST_FAILED("Count constructor: element [{0}] is {1}, expected 0", i, vec[i]);
		}

		TEST_PASSED();
	}

	void VectorTest::TestCountValueConstructor()
	{
		Core::Vector<int32> vec(4, 42);

		if (vec.Count() != 4)
			TEST_FAILED("Count+value constructor: expected count 4, got {0}", vec.Count());

		for (uint32 i = 0; i < vec.Count(); i++)
		{
			if (vec[i] != 42)
				TEST_FAILED("Count+value constructor: element [{0}] is {1}, expected 42", i, vec[i]);
		}

		TEST_PASSED();
	}

	void VectorTest::TestInitializerListConstructor()
	{
		Core::Vector<int32> vec{7, 14, 21, 28, 35};

		if (vec.Count() != 5)
			TEST_FAILED("Initializer list constructor: expected count 5, got {0}", vec.Count());

		int32 expected[] = {7, 14, 21, 28, 35};
		for (uint32 i = 0; i < vec.Count(); i++)
		{
			if (vec[i] != expected[i])
				TEST_FAILED("Initializer list constructor: element [{0}] is {1}, expected {2}", i, vec[i], expected[i]);
		}

		TEST_PASSED();
	}

	// ─── Accessor Tests ─────────────────────────────────────────────────────

	void VectorTest::TestOperatorIndex()
	{
		Core::Vector<int32> vec{100, 200, 300};

		// Read access via operator[]
		if (vec[0] != 100)
			TEST_FAILED("operator[] read: element [0] is {0}, expected 100", vec[0]);

		if (vec[1] != 200)
			TEST_FAILED("operator[] read: element [1] is {0}, expected 200", vec[1]);

		if (vec[2] != 300)
			TEST_FAILED("operator[] read: element [2] is {0}, expected 300", vec[2]);

		// Write access via operator[]
		vec[0] = 1;
		vec[1] = 2;
		vec[2] = 3;

		if (vec[0] != 1 || vec[1] != 2 || vec[2] != 3)
			TEST_FAILED("operator[] write: values not updated correctly");

		// Const access via operator[]
		const Core::Vector<int32>& constVec = vec;
		if (constVec[0] != 1 || constVec[1] != 2 || constVec[2] != 3)
			TEST_FAILED("operator[] const read: values mismatch");

		TEST_PASSED();
	}

	void VectorTest::TestFront()
	{
		Core::Vector<int32> vec{50, 60, 70};

		// Read via Front()
		if (vec.Front() != 50)
			TEST_FAILED("Front(): value is {0}, expected 50", vec.Front());

		// Write via Front() — verifies it returns a reference
		vec.Front() = 5;
		if (vec[0] != 5)
			TEST_FAILED("Front() write: element [0] is {0}, expected 5", vec[0]);

		// Const Front()
		const Core::Vector<int32>& constVec = vec;
		if (constVec.Front() != 5)
			TEST_FAILED("Front() const: value is {0}, expected 5", constVec.Front());

		TEST_PASSED();
	}

	void VectorTest::TestBack()
	{
		Core::Vector<int32> vec{1, 2, 3};

		// Read via Back()
		if (vec.Back() != 3)
			TEST_FAILED("Back(): value is {0}, expected 3", vec.Back());

		// Write via Back() — verifies it returns a reference
		vec.Back() = 30;
		if (vec[2] != 30)
			TEST_FAILED("Back() write: element [2] is {0}, expected 30", vec[2]);

		// Const Back()
		const Core::Vector<int32>& constVec = vec;
		if (constVec.Back() != 30)
			TEST_FAILED("Back() const: value is {0}, expected 30", constVec.Back());

		TEST_PASSED();
	}

	void VectorTest::TestData()
	{
		Core::Vector<int32> vec{10, 20, 30, 40};

		// Verify Data() returns a valid pointer
		const int32* data = vec.Data();
		if (data == nullptr)
			TEST_FAILED("Data(): returned null pointer");

		// Verify values through pointer
		if (data[0] != 10 || data[1] != 20 || data[2] != 30 || data[3] != 40)
			TEST_FAILED("Data(): values accessed through pointer do not match");

		// Verify memory contiguity via pointer arithmetic
		for (uint32 i = 0; i < vec.Count(); i++)
		{
			if (data[i] != vec[i])
				TEST_FAILED("Data() contiguity: data[{0}] is {1}, expected {2}", i, data[i], vec[i]);
		}

		// Verify pointer arithmetic matches element size
		for (uint32 i = 1; i < vec.Count(); i++)
		{
			const int32* expectedPtr = data + i;
			if (*expectedPtr != vec[i])
				TEST_FAILED("Data() pointer arithmetic: *(data+{0}) is {1}, expected {2}", i, *expectedPtr, vec[i]);
		}

		// Verify Data() returns contiguous memory: &vec[1] - &vec[0] == 1
		int32* mutableData = vec.Data();
		ptrdiff_t diff = &mutableData[1] - &mutableData[0];
		if (diff != 1)
			TEST_FAILED("Data() contiguity: pointer difference is {0}, expected 1", diff);

		// Write through Data() pointer — verifies mutable access
		mutableData[0] = 100;
		if (vec[0] != 100)
			TEST_FAILED("Data() write: vec[0] is {0}, expected 100", vec[0]);

		// Const Data()
		const Core::Vector<int32>& constVec = vec;
		const int32* constData = constVec.Data();
		if (constData[0] != 100)
			TEST_FAILED("Data() const: value is {0}, expected 100", constData[0]);

		TEST_PASSED();
	}

	// ─── Existing Tests ─────────────────────────────────────────────────────

	void VectorTest::TestPushBackElement()
	{
		Core::Vector<int32> vec;
		vec.Push(10);
		vec.Push(20);
		vec.Push(30);

		if (vec.Count() != 3)
			TEST_FAILED("Expected vector size 3");
		if (vec[0] != 10 || vec[1] != 20 || vec[2] != 30)
			TEST_FAILED("Expected elements to be 10, 20, 30");

		TEST_PASSED();
	}

	void VectorTest::TestResizeUpAndDown()
	{
		Core::Vector<int32> vec;
		vec.Push(1);
		vec.Push(2);

		// Resize up
		vec.Resize(5, 0);
		if (vec.Count() != 5)
			TEST_FAILED("Expected size 5 after resize up");
		if (vec[0] != 1 || vec[1] != 2)
			TEST_FAILED("Original elements should be preserved");

		// Resize down
		vec.Resize(3);
		if (vec.Count() != 3)
			TEST_FAILED("Expected size 3 after resize down");

		TEST_PASSED();
	}

	void VectorTest::TestSortAscending()
	{
		Core::Vector<int32> vec;
		vec.Push(3);
		vec.Push(1);
		vec.Push(4);
		vec.Push(1);
		vec.Push(5);

		std::sort(vec.begin(), vec.end());

		if (vec[0] != 1 || vec[1] != 1 || vec[2] != 3 || vec[3] != 4 || vec[4] != 5)
			TEST_FAILED("Expected sorted vector [1, 1, 3, 4, 5]");

		TEST_PASSED();
	}
}
