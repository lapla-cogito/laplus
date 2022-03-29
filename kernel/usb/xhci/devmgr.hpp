#pragma once
#include <cstddef>
#include <cstdint>
#include "error.hpp"
#include "usb/xhci/context.hpp"
#include "usb/xhci/device.hpp"

namespace usb::xhci {
	class DeviceManager {

	public:
		Error Initialize(size_t max_slots);
		DeviceContext** DeviceContexts() const;
		Device* FindByPort(uint8_t port_num, uint32_t route_string) const;
		Device* FindByState(enum Device::State state) const;
		Device* FindBySlot(uint8_t slot_id) const;
		Error AllocDevice(uint8_t slot_id, DoorbellRegister* dbreg);
		Error LoadDCBAA(uint8_t slot_id);
		Error Remove(uint8_t slot_id);

	private:
		DeviceContext** device_context_pointers_;
		size_t max_slots_;
		Device** devices_;
	};
}
