//エンドポイント設定周り
#pragma once
#include "error.hpp"

namespace usb {
	enum class EndpointType {
		kControl = 0,
		kIsochronous = 1,
		kBulk = 2,
		kInterrupt = 3,
	};

	class EndpointID {
	public:
		constexpr EndpointID() : addr_{ 0 } {}
		constexpr EndpointID(const EndpointID& ep_id) : addr_{ ep_id.addr_ } {}
		explicit constexpr EndpointID(int addr) : addr_{ addr } {}
		constexpr EndpointID(int ep_num, bool dir_in) : addr_{ ep_num << 1 | dir_in } {}

		EndpointID& operator =(const EndpointID& rhs) {
			addr_ = rhs.addr_;
			return *this;
		}

		//エンドポイントアドレス(0-31)
		int Address() const { return addr_; }
		//エンドポイント番号(0-15)
		int Number() const { return addr_ >> 1; }
		//入出力方向.Controlエンドポイントはtrue
		bool IsIn() const { return addr_ & 1; }

	private:
		int addr_;
	};

	constexpr EndpointID kDefaultControlPipeID{ 0, true };

	struct EndpointConfig {
		EndpointID ep_id;
		EndpointType ep_type;
		int max_packet_size;
		int interval;
	};

	inline bool operator==(EndpointID lhs, EndpointID rhs) {
		return lhs.Address() == rhs.Address();
	}
}