#include <cstdint>
class Box {
public:
	Box() : secret(1) {}
	int32_t Peek() const { return secret; }
	void Poke(int32_t v) { secret = v; }
private:
	int32_t secret;
};
int32_t Twice(int32_t v);
