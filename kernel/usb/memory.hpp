#pragma once
#include <cstddef>

namespace usb {
	static const size_t kMemoryPoolSize = 4096 * 32;

	void* AllocMem(size_t size, unsigned int alignment, unsigned int boundary);

	template <class T>
	T* AllocArray(size_t num_obj, unsigned int alignment, unsigned int boundary) {
		return reinterpret_cast<T*>(
			AllocMem(sizeof(T) * num_obj, alignment, boundary));
	}

	void FreeMem(void* p);

	template <class T, unsigned int Alignment = 64, unsigned int Boundary = 4096>
	class Allocator {
	public:
		using size_type = size_t;
		using pointer = T*;
		using value_type = T;

		Allocator() noexcept = default;
		Allocator(const Allocator&) noexcept = default;
		template <class U> Allocator(const Allocator<U>&) noexcept {}
		~Allocator() noexcept = default;
		Allocator& operator=(const Allocator&) = default;

		pointer allocate(size_type n) {
			return AllocArray<T>(n, Alignment, Boundary);
		}

		void deallocate(pointer p, size_type num) {
			FreeMem(p);
		}
	};
}