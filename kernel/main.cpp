#include<cstdint>
#include<cstddef>
#include<cstdio>
#include<numeric>
#include<vector>
#include"frame_buffer_config.hpp"
#include"graphics.hpp"
#include"mouse.hpp"
#include"font.hpp"
#include"console.hpp"
#include"pci.hpp"
#include"logger.hpp"
#include"usb/memory.hpp"
#include"usb/device.hpp"
#include"usb/classdriver/mouse.hpp"
#include"usb/xhci/xhci.hpp"
#include"usb/xhci/trb.hpp"


//ピクセルの色
struct PixelCol {
	uint8_t r, g, b;
};

//演算子定義
void* operator new(size_t size, void* buf) { return buf; }
void operator delete(void* obj) noexcept {}
//定義終了


const PixelColor kDesktopBGColor{ 45, 118, 237 };
const PixelColor kDesktopFGColor{ 255, 255, 255 };

/*
//return: 0 success, nonzero fail
int WritePixel(const FrameBufferConfig& config,
	int x, int y, const PixelCol& c) {
	const int pixel_position = config.pixels_per_scan_line * y + x;

	if (config.pixel_format == kPixelRGBesv8BitPerColor) {
		uint8_t* p = &config.frame_buffer[pixel_position * 4];
		p[0] = c.r, p[1] = c.g, p[2] = c.b;
	}
	else if (config.pixel_format == kPixelBGResv8BitPerColor) {
		uint8_t* p = &config.frame_buffer[pixel_position * 4];
		p[0] = c.b, p[1] = c.g, p[2] = c.r;
	}
	else { return -1; }
	return 0;
}
*/


class PixelWriter {
private:
	const FrameBufferConfig& config_;

public:
	PixelWriter(const FrameBufferConfig& config) :config_{ config } {}
	virtual ~PixelWriter() = default;
	virtual void write(int x, int y, const PixelCol& c) = 0;

protected:
	uint8_t* PixelAt(int x, int y) { retunr config_.frame_buffer + 4 * (config_.pixels_per_scan_line * y + x); }
};

//PixelWriterを継承したクラス群
class RGBResv8bitPerColorPixelWriter :public PixelWriter {
public:
	using PixelWrier::PixelWriter;

	virtual void write(int x, int y, PixelCol& c) override {
		auto p = PixelAt(x, y);
		p[0] = c.r, p[1] = c.g, p[2] = c.b;
	}
};

class BGRResv8bitPerColorPixelWriter :public PixelWriter {
public:
	using PixelWrier::PixelWriter;

	virtual void write(int x, int y, PixelCol& c) override {
		auto p = PixelAt(x, y);
		p[0] = c.b, p[1] = c.g, p[2] = c.r;
	}
};
//クラス群終了

//コンソールクラスのバッファ
char console_buf[sizeof(Console)];
Console* console;
//バッファ終了

//Linuxのprintkと同様
int printk(const char* format, ...) {
	va_list ap;
	int result;
	char s[1024];
	va_start(ap, format);
	result = vsprintf(s, format, ap);
	va_end(ap);
	console->PutString(s);
	return result;
}
//printk終了

char mouse_cursor_buf[sizeof(MouseCursor)];
MouseCursor* mouse_cursor;

void MouseObserver(int8_t displacement_x, int8_t displacement_y) { mouse_cursor->MoveRelative({ displacement_x, displacement_y }); }

//USBポートの制御モード切替用関数
void SwitchEhci2Xhci(const pci::Device& xhc_dev) {
	bool intel_ehc_exist = false;
	//Intel製のコントローラーが存在するか確認
	for (int i = 0; i < pci::num_device; ++i) {
		if (pci::devices[i].class_code.Match(0x0cu, 0x03u, 0x20u) /* EHCI */ &&
			0x8086 == pci::ReadVendorId(pci::devices[i])) {
			intel_ehc_exist = true;
			break;
		}
	}
	if (!intel_ehc_exist) { return; }

	//あればEHCIからxHCIへのモード切り替えを実行
	uint32_t superspeed_ports = pci::ReadConfReg(xhc_dev, 0xdc); // USB3PRM
	pci::WriteConfReg(xhc_dev, 0xd8, superspeed_ports); // USB3_PSSEN
	uint32_t ehci2xhci_ports = pci::ReadConfReg(xhc_dev, 0xd4); // XUSB2PRM
	pci::WriteConfReg(xhc_dev, 0xd0, ehci2xhci_ports); // XUSB2PR
	Log(kDebug, "SwitchEhci2Xhci: SS = %02, xHCI = %02x\n",
		superspeed_ports, ehci2xhci_ports);
}
//切り替え用関数終了

//エントリーポイント
extern "C" void KernelMain(const FrameBufferConfig & frame_buffer_config) {
	switch (frame_buffer_config.pixel_format) {
	case kPixelRGBResv8BitPerColor:
		pixel_writer = new(pixel_writer_buf)
			RGBResv8BitPerColorPixelWriter{ frame_buffer_config };
		break;
	case kPixelBGRResv8BitPerColor:
		pixel_writer = new(pixel_writer_buf)
			BGRResv8BitPerColorPixelWriter{ frame_buffer_config };
		break;
	}

	//白塗りつぶし
	for (int x = 0; x < frame_buffer_config.horizontal_resolution; ++x) {
		for (int y = 0; y < frame_buffer_config.vertical_resolution; ++y) { pixel_writer->Write(x, y, { 255, 255, 255 }); }
	}

	FillRectangle(*pixel_writer,
		{ 0, 0 },
		{ kFrameWidth, kFrameHeight - 50 },
		kDesktopBGColor);
	FillRectangle(*pixel_writer,
		{ 0, kFrameHeight - 50 },
		{ kFrameWidth, 50 },
		{ 1, 8, 17 });
	FillRectangle(*pixel_writer,
		{ 0, kFrameHeight - 50 },
		{ kFrameWidth / 5, 50 },
		{ 80, 80, 80 });
	DrawRectangle(*pixel_writer,
		{ 10, kFrameHeight - 40 },
		{ 30, 30 },
		{ 160, 160, 160 });


	//ASCII文字を1列に描画
	for (int i = 0, char c = '!'; c <= '~'; ++c, ++i) { WriteAscii(*pixel_writer, 8 * i, 50, c, { 0, 0, 0 }); }

	//新規コンソール
	console = new(console_buf) Console{ *pixel_writer, {0, 0, 0}, {255, 255, 255} };

	//printkを用いたコンソール表示
	for (int i = 0; i < 27; ++i) { printk("printk: %d\n", i); }

	//MouseCursorクラスのコンストラクタを生成
	mouse_cursor = new(mouse_cursor_buf) MouseCursor{
	pixel_writer, kDesktopBGColor, {300, 200}
	};
	//生成終了

	//PCIデバイスの列挙
	auto err = pci::ScanAllBus();
	printk("ScanAllBus: %s\n", err.Name());

	for (int i = 0; i < pci::num_device; ++i) {
		const auto& dev = pci::devices[i];
		auto vendor_id = pci::ReadVendorId(dev.bus, dev.device, dev.function);
		auto class_code = pci::ReadClassCode(dev.bus, dev.device, dev.function);
		printk("%d.%d.%d: vend %04x, class %08x, head %02x\n",
			dev.bus, dev.device, dev.function,
			vendor_id, class_code, dev.header_type);
	}
	//列挙終了

	//Intel製を優先してPCIバスからxHCデバイスを探索
	//クラスコードが0x0c,0x03,0x30のものを探す
	pci::Device* xhc_dev = nullptr;
	for (int i = 0; i < pci::num_device; ++i) {
		if (pci::devices[i].class_code.Match(0x0cu, 0x03u, 0x30u)) {
			xhc_dev = &pci::devices[i];

			if (0x8086 == pci::ReadVendorId(*xhc_dev)) { break; }
		}
	}

	//xhc_devがnullで無ければxHCが存在する
	if (xhc_dev) { Log(kInfo, "xHC has been found: %d.%d.%d\n", xhc_dev->bus, xhc->device, xhc_dev->function); }
	//探索終了

	//BAR0レジスタを読み取り
	//xHCのレジスタ群が存在するメモリアドレスを取得したい
	//xHCはMMIO(Memory-Mapped I/O)によって制御されている
	//MMIOはメモリと同様に読み書きできるレジスタでMMIOアドレスはPCIコンフィギュレーション空間内のBAR0(Base Address Register 0)に記録されている
	const WithError<uint64_t> xhc_bar = pci::ReadBar(*xhc_dev, 0);
	Log(kDebug, "ReadBar: %s\n", xhc_bar.error.Name());
	const uint64_t xhc_mmio_base = xhc_bar.value & ~static_cast<uint64_t>(0xf);
	Log(kDebug, "xHC mmio_base = %08lx\n", xhc_mmio_base);
	//読み取り終了

	sb::xhci::Controller xhc{ xhc_mmio_base };

	//xHCの初期化
	if (0x8086 == pci::ReadVendorId(*xhc_dev)) {
		SwitchEhci2Xhci(*xhc_dev);
	}
	{
		auto err = xhc.Initialize();
		Log(kDebug, "xhc.Initialize: %s\n", err.Name());
	}

	Log(kInfo, "xHC starting...\n");
	//初期化終了

	//xHC起動
	xhc.Run();
	//起動終了

	//USBポートを調べる
	usb::HIDMouseDriver::default_observer = MouseObserver;
	for (int i = 1; i <= xhc.MaxPorts(); ++i) {
		auto port = xhc.PortAt(i);
		Log(kDebug, "Port %d: IsConnected=%d\n", i, port.IsConnected());
		//接続済みのポートには適宜設定(ポートリセット,xHCの内部設定,クラスドライバの生成 etc...)を行う
		if (port.IsConnected()) {
			if (auto err = ConfigurePort(xhc, port)) {
				Log(kError, "failed to configure port: %s at %s:%d\n",
					err.Name(), err.File(), err.Line());
				continue;
			}
		}
	}
	//調査終了

	//xHCに溜まったイベントの処理
	while (1) {
		if (auto err = ProcessEvent(xhc)) {
			Log(kError, "Error while ProcessEvent: %s at %s:%d\n",
				err.Name(), err.File(), err.Line());
		}
	}
	//処理終了

	while (1) __asm__("hlt");
}