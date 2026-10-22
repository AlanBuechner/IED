#pragma once
#include <filesystem>


namespace fs = std::filesystem;

#if defined(STRIP_CLANG)
	#define DISABLE_OPS
#else
	#define DISABLE_OPS __attribute__((optnone))
#endif

#define DEFINE_HAS_FUNC(F) \
template <typename T>\
class HasFunc##F {\
	typedef char one;\
	struct two { char x[2]; };\
	template <typename C> static one test(decltype(&C::F));\
	template <typename C> static two test(...);\
public:\
	enum { value = sizeof(test<T>(0)) == sizeof(char) };\
};\
template<class T> constexpr bool Has##F = HasFunc##F<T>::value;

namespace Core
{

}
