// Inheritance and Polymorphism Tests - Header

struct Shape {
	float x;
	float y;
	
	[operator("create")]
	void Init();
	
	[operator("create")]
	void Init(float nx, float ny);
	
	virtual float GetArea();
};

struct Circle : public Shape {
	float radius;
	
	[operator("create")]
	void Init();
	
	[operator("create")]
	void Init(float cx, float cy, float r);
	
	float GetArea() override;
};

struct Rectangle : public Shape {
	float width;
	float height;
	
	[operator("create")]
	void Init();
	
	[operator("create")]
	void Init(float x, float y, float w, float h);
	
	float GetArea() override;
};

// Polymorphism test - takes base class pointer
float CalculateArea(Shape* shape);

// Test functions
void TestInheritanceBasic();
void TestVirtualDispatch();
void TestPolymorphism();
