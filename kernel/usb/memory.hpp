//USB ドライバ用の動的メモリ管理機能

#pragma once
#include <cstddef>

namespace usb {
	//動的メモリ確保のためのメモリプールの最大容量(bytes)
	static const size_t kMemoryPoolSize = 4096 * 32;

	//指定されたバイト数のメモリ領域を確保して先頭ポインタを返す関数
	/*先頭アドレスがalignmentに揃ったメモリ領域を確保する
	size <= boundaryならメモリ領域がboundaryを跨がないことを保証する
	boundaryは典型的にはページ境界を跨がないように4096を設定
	size:=確保するメモリ領域のサイズ(bytes)
	alignment:=メモリ領域のアライメント制約.0なら制約しない
	boundary:=確保したメモリ領域が跨いではいけない境界.0 なら制約しない
	確保できなかった場合は nullptrを返す
	*/
	void* AllocMem(size_t size, unsigned int alignment, unsigned int boundary);

	template <class T>
	T* AllocArray(size_t num_obj, unsigned int alignment, unsigned int boundary) {
		return reinterpret_cast<T*>(
			AllocMem(sizeof(T) * num_obj, alignment, boundary));
	}

	//指定されたメモリ領域を解放しようとする.保証はされないことに注意
	void FreeMem(void* p);

	//標準コンテナ用のメモリアロケータ
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
