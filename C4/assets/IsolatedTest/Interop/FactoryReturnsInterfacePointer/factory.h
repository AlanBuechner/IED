#include <cstdint>
class IShape {
public:
    virtual ~IShape();
    virtual int32_t Area() const = 0;
};
class Circle : public IShape {
public:
    explicit Circle(int32_t radius);
    ~Circle();
    int32_t Area() const override;
private:
    int32_t radius;
};
IShape* CreateShape(int32_t kind, int32_t radius);
void ReleaseShape(IShape* s);
