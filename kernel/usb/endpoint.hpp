//エンドポイント設定に関する機能
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

		/** エンドポイント番号と入出力方向からIDを構成する．
		 * ep_numは0-15の整数
		 * dir_inはControlエンドポイントでは常にtrueにしなければならない．
		 */
		constexpr EndpointID(int ep_num, bool dir_in) : addr_{ ep_num << 1 | dir_in } {}

		EndpointID& operator =(const EndpointID& rhs) {
			addr_ = rhs.addr_;
			return *this;
		}

		//エンドポイントアドレス(0-31)
		int Address() const { return addr_; }

		//エンドポイント番号(0-15)
		int Number() const { return addr_ >> 1; }

		//入出力方向．Controlエンドポイントはtrue
		bool IsIn() const { return addr_ & 1; }

	private:
		int addr_;
	};

	constexpr EndpointID kDefaultControlPipeID{ 0, true };

	struct EndpointConfig {
		//エンドポイントID
		EndpointID ep_id;

		//エンドポイントの種別
		EndpointType ep_type;

		//エンドポイントの最大パケットサイズ(bytes)
		int max_packet_size;

		//このエンドポイントの制御周期(125*2^(interval-1) [micro sec.])
		int interval;
	};
}