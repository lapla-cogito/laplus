#pragma once
#include <cstdint>
#include <vector>
#include "error.hpp"
#include "usb/memory.hpp"
#include "usb/xhci/registers.hpp"
#include "usb/xhci/trb.hpp"

namespace usb::xhci {
	class Ring {
	public:
		Ring() = default;
		Ring(const Ring&) = delete;
		~Ring();
		Ring& operator=(const Ring&) = delete;
		Error Initialize(size_t buf_size);

		template <typename TRBType>
		TRB* Push(const TRBType& trb) {
			return Push(trb.data);
		}

		TRB* Buffer() const { return buf_; }

	private:
		TRB* buf_ = nullptr;
		size_t buf_size_ = 0;

		bool cycle_bit_;
		size_t write_index_;

		void CopyToLast(const std::array<uint32_t, 4>& data);

		TRB* Push(const std::array<uint32_t, 4>& data);
	};

	union EventRingSegmentTableEntry {
		std::array<uint32_t, 4> data;
		struct {
			uint64_t ring_segment_base_address;

			uint32_t ring_segment_size : 16;
			uint32_t : 16;

			uint32_t : 32;
		} __attribute__((packed)) bits;
	};

	class EventRing {
	public:
		Error Initialize(size_t buf_size, InterrupterRegisterSet* interrupter);

		TRB* ReadDequeuePointer() const {
			return reinterpret_cast<TRB*>(interrupter_->ERDP.Read().Pointer());
		}

		void WriteDequeuePointer(TRB* p);

		bool HasFront() const {
			return Front()->bits.cycle_bit == cycle_bit_;
		}

		TRB* Front() const {
			return ReadDequeuePointer();
		}

		void Pop();

	private:
		TRB* buf_;
		size_t buf_size_;

		bool cycle_bit_;
		EventRingSegmentTableEntry* erst_;
		InterrupterRegisterSet* interrupter_;
	};
}