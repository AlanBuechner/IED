#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace Scripting
{
	/// The storage of one HIR tree: objects are placed in large blocks and destroyed together with the arena, so making
	/// one is a pointer bump instead of a heap allocation and the tree is freed a block at a time. Addresses are stable.
	class HIRArena
	{
	public:
		HIRArena() = default;
		HIRArena(const HIRArena&) = delete;
		HIRArena& operator=(const HIRArena&) = delete;
		HIRArena(HIRArena&& other) noexcept { Take(other); }
		HIRArena& operator=(HIRArena&& other) noexcept
		{
			if (this != &other)
			{
				Release();
				Take(other);
			}
			return *this;
		}
		~HIRArena() { Release(); }

		template <typename T, typename... Args>
		T* Make(Args&&... args)
		{
			T* made = new (Allocate(sizeof(T), alignof(T))) T(std::forward<Args>(args)...);
			if constexpr (!std::is_trivially_destructible_v<T>) m_Destructors.push_back({ made, [](void* object) { static_cast<T*>(object)->~T(); } });
			return made;
		}

	private:
		struct Destructor
		{
			void* object;
			void (*destroy)(void*);
		};

		static constexpr size_t kBlockBytes = 32 * 1024;

		void* Allocate(size_t bytes, size_t align)
		{
			std::byte* at = AlignUp(m_Next, align);
			if (!m_Next || at + bytes > m_End)
			{
				const size_t size = bytes + align > kBlockBytes ? bytes + align : kBlockBytes;
				m_Blocks.push_back(std::make_unique_for_overwrite<std::byte[]>(size));
				m_Next = m_Blocks.back().get();
				m_End = m_Next + size;
				at = AlignUp(m_Next, align);
			}
			m_Next = at + bytes;
			return at;
		}

		static std::byte* AlignUp(std::byte* p, size_t align)
		{
			const auto address = reinterpret_cast<uintptr_t>(p);
			return reinterpret_cast<std::byte*>((address + align - 1) & ~(uintptr_t(align) - 1));
		}

		void Release()
		{
			for (auto it = m_Destructors.rbegin(); it != m_Destructors.rend(); ++it) it->destroy(it->object);
			m_Destructors.clear();
			m_Blocks.clear();
			m_Next = m_End = nullptr;
		}

		void Take(HIRArena& other)
		{
			m_Blocks = std::move(other.m_Blocks);
			m_Destructors = std::move(other.m_Destructors);
			m_Next = std::exchange(other.m_Next, nullptr);
			m_End = std::exchange(other.m_End, nullptr);
		}

		std::vector<std::unique_ptr<std::byte[]>> m_Blocks;
		std::vector<Destructor> m_Destructors;
		std::byte* m_Next = nullptr;
		std::byte* m_End = nullptr;
	};
} // namespace Scripting
