#pragma once
#include "Math/Math.h"
#include "Platform/PlatformCommon.h"
#include "Core/Common.h"
#include "Core/Utils.h"
#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/SparseMap.h"
#include "Core/Logging/Log.h"
#include <memory>
#include <concepts>
#include <functional>
#include <type_traits>

#define ENABLE_ASSERTS

#ifdef _WIN32
	#ifdef _WIN64
		#define PLATFORM_WINDOWS
	#else
		#error "x86 Buillds are not sapported"
	#endif // _WIN64
#elif defined(__APPLE__) || defined(__MACH__)
	#include <TargetConditionals.h>
	#if TARGET_IPHONE_SIMULATOR == 1
		#error "IOS simulator is not sapported"
	#elif TARGET_OS_IPHONE == 1
		#define PLATFORM_IOS
		#error "IOS is not sapported"
	#elif TARGET_OS_MAC == 1
		#define PLATFORM_MACOS
		#error "MacOS is not sapported"
	#endif
#elif defined(__ANDROID__)
	#define PLATFORM_ANDROID
	#error "Android is not sapported"
#elif defined(__linux__)
	#define PLATFORM_LINUX
	#error "Linux is not sapported"
#else
	#error "Unknown Platform!"
#endif

#ifdef ENABLE_ASSERTS
#define CORE_ASSERT_FUNC(x, func, msg, ...) {if(!(x)) {CORE_ERROR((std::string("Assertion Failed: ") + (msg)).c_str(), __VA_ARGS__); func;}}
	#define CORE_ASSERT(x, msg, ...) CORE_ASSERT_FUNC(x, __debugbreak(), msg, __VA_ARGS__)
	#define CORE_ASSERT_BREAK(x, msg, ...) CORE_ASSERT_FUNC(x, break, msg, __VA_ARGS__)
	#define CORE_ASSERT_CONTINUE(x, msg, ...) CORE_ASSERT_FUNC(x, continue, msg, __VA_ARGS__)
	#define CORE_ASSERT_RETURN(x, msg, ...) CORE_ASSERT_FUNC(x, return, msg, __VA_ARGS__)
#else
	#define CORE_ASSERT_FUNC(...)
	#define CORE_ASSERT(...)
	#define CORE_ASSERT_BREAK(...)
	#define CORE_ASSERT_CONTINUE(...)
	#define CORE_ASSERT_RETURN(...)
#endif // ENABLE_ASSERTS


#define BIT(x) (1 << (x))

#define MEM_KiB(kib)	((kib) * 1024)
#define MEM_MiB(mib)	MEM_KiB((mib) * 1024)
#define MEM_GiB(gib)	MEM_MiB((gib) * 1024)

#define DISABLE_COPY(type) type(const type&) = delete;
#define DISABLE_MOVE(type) type(const type&&) = delete;
#define DISABLE_COPY_MOVE(type)\
	DISABLE_COPY(type)\
	DISABLE_MOVE(type)

#define TOKEN_PASTE(x, y) x##y
#define CAT(x,y) TOKEN_PASTE(x,y)

#define VAL(str) #str
#define TO_STRING(str) VAL(str)

class Refable
{
public:
	inline uint32 GetRefCount() { return m_Count; }

private:
	inline uint32 AddRef() { return ++m_Count; }
	inline uint32 Release() { return --m_Count; }

private:
	std::atomic<uint32> m_Count = 0;

	template<class T> friend class Ref;
};

template<class To, class From>
concept ConvertableTo = std::is_convertible_v<From, To> || std::is_same_v<From, To> || std::is_base_of_v<To, From>;

DEFINE_HAS_FUNC(ReportRefChange);
DEFINE_HAS_FUNC(ReportRefAdd);
DEFINE_HAS_FUNC(ReportRefRelease);

#define ASSERT_REFABLE static_assert(ConvertableTo<Refable, T>)
#define ASSERT_CASTABLE static_assert(ConvertableTo<T, T2>)


template<class T>
class Ref
{
public:
	inline Ref(T* ref = nullptr) : m_Refable(ref) { ASSERT_REFABLE; AddRef(); }
	
	inline Ref(const Ref<T>& other) : m_Refable(other.Get()) { ASSERT_REFABLE; AddRef(); }
	inline Ref(Ref<T>&& other) : m_Refable(other.Get()) { ASSERT_REFABLE; other.m_Refable = nullptr; }

	template<class T2>
	inline Ref(const Ref<T2>& other) : m_Refable(other.Get()) { ASSERT_REFABLE; ASSERT_CASTABLE; AddRef(); }
	template<class T2>
	inline Ref(Ref<T2>&& other) : m_Refable(other.Get()) { ASSERT_REFABLE; ASSERT_CASTABLE; AddRef(); }

	inline ~Ref() { Release(); }

	template<class T2>
	Ref<T>& Assign(const Ref<T2>& other) {
		ASSERT_REFABLE;
		ASSERT_CASTABLE;
		if (*this == other)
			return *this;

		Release();
		m_Refable = (T*)other.Get();
		AddRef();
		return *this;
	}

	template<class T2>
	Ref<T>& operator=(const Ref<T2>& other){ return Assign<T2>(other); }
	Ref<T>& operator=(const Ref<T>& other) { return Assign<T>(other); }

	template<class T2>
	bool operator==(const Ref<T2>& other) const	{ASSERT_CASTABLE;return m_Refable == (T*)other.Get();}
	template<class T2>
	bool operator==(T2* other) const			{ ASSERT_CASTABLE; return m_Refable == (T*)other; }
	bool operator==(T* other) const				{ return m_Refable == other; }

	template<class T2>
	bool operator!=(const Ref<T2>& other) const	{ ASSERT_CASTABLE; return m_Refable != (T*)other.Get(); }
	template<class T2>
	bool operator!=(T2* other) const			{ ASSERT_CASTABLE; return m_Refable != (T*)other; }
	bool operator!=(T* other) const				{ return m_Refable != other; }

	bool Valid() const { return m_Refable != nullptr; }
	operator bool() const { return Valid(); }
	inline T* Get() const { return m_Refable; }
	inline T* operator->() { return Get(); }
	inline const T* operator->() const { return Get(); }
	inline T& operator*() { return *Get(); }
	inline const T& operator*() const { return *Get(); }

	// WARNING : does not preform type checks
	template<class T2>
	Ref<T2> As() const {
		return Ref<T2>((T2*)m_Refable);
	}

	template<class T2>
	Ref<T2> Cast() const {
		return Ref<T2>(dynamic_cast<T2*>(m_Refable));
	}

	template<class T2>
	T2* PointerCast() const {
		return dynamic_cast<T2*>(m_Refable);
	}

	void Reset() { Release(); }

private:

	void AddRef() {
		if (!Valid()) return;
		m_Refable->AddRef();
		if constexpr (HasReportRefChange<T>) { m_Refable->ReportRefChange(); }
		if constexpr (HasReportRefAdd<T>) { m_Refable->ReportRefAdd(); }
	}
	void Release() {
		if (!Valid()) return;
		bool needsDelete = m_Refable->Release() == 0;
		if constexpr (HasReportRefChange<T>) { m_Refable->ReportRefChange(); }
		if constexpr (HasReportRefRelease<T>) { m_Refable->ReportRefRelease(); }
		if (needsDelete) {
			delete m_Refable;
		}
		m_Refable = nullptr;
	}

private:
	T* m_Refable = nullptr;
};

#undef ASSERT_REFABLE
#undef ASSERT_CASTABLE

template<typename T, typename ... Args>
inline constexpr Ref<T> CreateRef(Args&& ... args)
{
	return Ref(new T(std::forward<Args>(args)...));
}

template<typename T>
struct RefHash {
	std::size_t operator()(const Ref<T>& ref) const {
		return (size_t)ref.Get();
	}
};

namespace std
{

	template <typename T>
	struct hash<Ref<T>>
	{
		size_t operator()(const Ref<T>& ref) const {
			RefHash<T> hasher;
			return hasher(ref);
		}
	};
}
