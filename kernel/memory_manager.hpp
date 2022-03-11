//メモリ管理用のクラスとその周辺機能
#pragma once
#include <array>
#include <limits>
#include "error.hpp"
#include "memory_map.hpp"

namespace {
	constexpr unsigned long long operator""_KiB(unsigned long long kib) { return kib * 1024; }

	constexpr unsigned long long operator""_MiB(unsigned long long mib) { return mib * 1024_KiB; }

	constexpr unsigned long long operator""_GiB(unsigned long long gib) { return gib * 1024_MiB; }
}

//物理メモリフレーム1つの大きさ(bytes)
static const auto kBytesPerFrame{ 4_KiB };

class FrameID {
public:
	explicit FrameID(size_t id) : id_{ id } {}
	size_t ID() const { return id_; }
	void* Frame() const { return reinterpret_cast<void*>(id_ * kBytesPerFrame); }

private:
	size_t id_;
};

static const FrameID kNullFrame{ std::numeric_limits<size_t>::max() };

struct MemoryStat {
	size_t allocated_frames;
	size_t total_frames;
};

/*ビットマップ配列を用いてフレーム単位でメモリ管理するクラス
 *
 * 1ビットを1フレームに対応させて,ビットマップにより空きフレームを管理
 * 配列alloc_mapの各ビットがフレームに対応していて,0 なら空き,1 なら使用中
 * alloc_map[n]のmビット目が対応する物理アドレスは次式: kFrameBytes * (n * kBitsPerMapLine + m)
 */
class BitmapMemoryManager {
public:
	//BitmapMemoryManagerで扱える最大の物理メモリ量(bytes)
	static const auto kMaxPhysicalMemoryBytes{ 128_GiB };
	//kMaxPhysicalMemoryBytesまでの物理メモリを扱うために必要なフレーム数
	static const auto kFrameCount{ kMaxPhysicalMemoryBytes / kBytesPerFrame };

	//ビットマップ配列の要素型
	using MapLineType = unsigned long;
	//ビットマップ配列の1つの要素のビット数==フレーム数
	static const size_t kBitsPerMapLine{ 8 * sizeof(MapLineType) };

	//インスタンス初期化
	BitmapMemoryManager();

	//要求されたフレーム数の領域を確保して先頭のフレームIDを返す
	WithError<FrameID> Allocate(size_t num_frames);
	Error Free(FrameID start_frame, size_t num_frames);
	void MarkAllocated(FrameID start_frame, size_t num_frames);

	//このメモリマネージャで扱うメモリ範囲を設定.この呼び出し以降ではAllocateによるメモリ割り当ては設定された範囲内でのみ行われる.range_begin_はメモリ範囲の始点,range_end_はメモリ範囲の終点で最終フレームの次のフレームを指す
	void SetMemoryRange(FrameID range_begin, FrameID range_end);

	//空き/総フレームの数を返す
	MemoryStat Stat() const;

private:
	std::array<MapLineType, kFrameCount / kBitsPerMapLine> alloc_map_;
	//このメモリマネージャで扱うメモリ範囲の始点
	FrameID range_begin_;
	//このメモリマネージャで扱うメモリ範囲の終点で最終フレームの次のフレーム
	FrameID range_end_;

	bool GetBit(FrameID frame) const;
	void SetBit(FrameID frame, bool allocated);
};

extern BitmapMemoryManager* memory_manager;
void InitializeMemoryManager(const MemoryMap& memory_map);