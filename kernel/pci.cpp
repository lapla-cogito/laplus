#include "pci.hpp"
#include "asmfunc.h"

namespace {
	using namespace pci;

	//CONFIG_ADDRESSレジスタに書き込むための32bit値生成
	uint32_t MakeAddress(uint8_t bus, uint8_t device,
		uint8_t function, uint8_t reg_addr) {
		auto shl = [](uint32_t x, unsigned int bits) {
			return x << bits;
		};

		return shl(1, 31)
			| shl(bus, 16)
			| shl(device, 11)
			| shl(function, 8)
			| (reg_addr & 0xfcu);
	}
	//生成終了

	//発見したPCIデバイスをdevicesに追加
	Error AddDevice(uint8_t bus, uint8_t device,
		uint8_t function, uint8_t header_type) {
		if (num_device == devices.size()) {
			return Error::kFull;
		}

		devices[num_device] = Device{ bus, device, function, header_type };
		++num_device;
		return Error::kSuccess;
	}
	//devices追加完了

	Error ScanBus(uint8_t bus);

	//指定のファンクションを devices に追加する.もし PCI-PCI ブリッジなら，セカンダリバスに対し ScanBus を実行する.
	Error ScanFunction(uint8_t bus, uint8_t device, uint8_t function) {
		auto header_type = ReadHeaderType(bus, device, function);
		if (auto err = AddDevice(bus, device, function, header_type)) {
			return err;
		}

		auto class_code = ReadClassCode(bus, device, function);
		uint8_t base = (class_code >> 24) & 0xffu;
		uint8_t sub = (class_code >> 16) & 0xffu;

		if (base == 0x06u && sub == 0x04u) {
			// standard PCI-PCI bridge
			auto bus_numbers = ReadBusNumbers(bus, device, function);
			uint8_t secondary_bus = (bus_numbers >> 8) & 0xffu;
			return ScanBus(secondary_bus);
		}

		return Error::kSuccess;
	}
	//ファンクション追加完了

	//指定のデバイス番号の各ファンクションをスキャンする.有効なファンクションを見つけたらScanFunctionを実行する.
	Error ScanDevice(uint8_t bus, uint8_t device) {
		if (auto err = ScanFunction(bus, device, 0)) {
			return err;
		}
		if (IsSingleFunctionDevice(ReadHeaderType(bus, device, 0))) {
			return Error::kSuccess;
		}

		for (uint8_t function = 1; function < 8; ++function) {
			if (ReadVendorId(bus, device, function) == 0xffffu) {
				continue;
			}
			if (auto err = ScanFunction(bus, device, function)) {
				return err;
			}
		}
		return Error::kSuccess;
	}
	//ファンクションスキャン終了

	//指定のバス番号の各デバイスをスキャンする.有効なデバイスを見つけたら ScanDevice を実行する.
	Error ScanBus(uint8_t bus) {
		for (uint8_t device = 0; device < 32; ++device) {
			if (ReadVendorId(bus, device, 0) == 0xffffu) {
				continue;
			}
			if (auto err = ScanDevice(bus, device)) {
				return err;
			}
		}
		return Error::kSuccess;
	}
	//デバイススキャン終了
}

namespace pci {
	void WriteAddress(uint32_t address) { IoOut32(kConfigAddress, address); }

	void WriteData(uint32_t value) { IoOut32(kConfigData, value); }

	uint32_t ReadData() { return IoIn32(kConfigData); }

	uint16_t ReadVendorId(uint8_t bus, uint8_t device, uint8_t function) {
		WriteAddress(MakeAddress(bus, device, function, 0x00));
		return ReadData() & 0xffffu;
	}

	uint16_t ReadDeviceId(uint8_t bus, uint8_t device, uint8_t function) {
		WriteAddress(MakeAddress(bus, device, function, 0x00));
		return ReadData() >> 16;
	}

	uint8_t ReadHeaderType(uint8_t bus, uint8_t device, uint8_t function) {
		WriteAddress(MakeAddress(bus, device, function, 0x0c));
		return (ReadData() >> 16) & 0xffu;
	}

	uint32_t ReadClassCode(uint8_t bus, uint8_t device, uint8_t function) {
		WriteAddress(MakeAddress(bus, device, function, 0x08));
		return ReadData();
	}

	uint32_t ReadBusNumbers(uint8_t bus, uint8_t device, uint8_t function) {
		WriteAddress(MakeAddress(bus, device, function, 0x18));
		return ReadData();
	}

	bool IsSingleFunctionDevice(uint8_t header_type) { return (header_type & 0x80u) == 0; }

	//PCIバスに接続されたデバイスを再帰的に探索
	Error ScanAllBus() {
		num_device = 0;

		auto header_type = ReadHeaderType(0, 0, 0);
		if (IsSingleFunctionDevice(header_type)) {
			return ScanBus(0);
		}

		for (uint8_t function = 1; function < 8; ++function) {
			if (ReadVendorId(0, 0, function) == 0xffffu) {
				continue;
			}
			if (auto err = ScanBus(function)) {
				return err;
			}
		}
		return Error::kSuccess;
	}
	//探索終了

	uint32_t ReadConfReg(const Device& dev, uint8_t reg_addr) {
		WriteAddress(MakeAddress(dev.bus, dev.device, dev.function, reg_addr));
		return ReadData();
	}

	void WriteConfReg(const Device& dev, uint8_t reg_addr, uint32_t value) {
		WriteAddress(MakeAddress(dev.bus, dev.device, dev.function, reg_addr));
		WriteData(value);
	}

	//指定されたBARとその後のBARを読み込んで結合して返す
	//WithErrorはerror.hppで定義
	WithError<uint64_t> ReadBar(Device& device, unsigned int bar_index) {
		if (bar_index >= 6) {
			return { 0, MAKE_ERROR(Error::kIndexOutOfRange) };
		}

		const auto addr = CalcBarAddress(bar_index);
		const auto bar = ReadConfReg(device, addr);

		// 32 bit address
		if ((bar & 4u) == 0) {
			return { bar, MAKE_ERROR(Error::kSuccess) };
		}

		// 64 bit address
		if (bar_index >= 5) {
			return { 0, MAKE_ERROR(Error::kIndexOutOfRange) };
		}

		const auto bar_upper = ReadConfReg(device, addr + 4);
		return {
		  bar | (static_cast<uint64_t>(bar_upper) << 32),
		  MAKE_ERROR(Error::kSuccess)
		};
	}
	//返し終了
}