#include<cstdint>
#include<cstddef>
#include"frame_buffer_config.hpp"
#include"font.hpp"
#include"graphics.hpp"
#include"console.hpp"


//ピクセルの色
struct PixelCol {
	uint8_t r, g, b;
};

//演算子定義
void* operator new(size_t size, void* buf) { return buf; }
void operator delete(void* obj) noexcept {}
//定義終了

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

	//座標(0,0)から200x100の(0,255,0)色のrectを描画
	for (int x = 0; x < 200; ++x) {
		for (int y = 0; y < 100; ++y) { pixel_writer->Write(x, y, { 0, 0, 255 }); }
	}


	//ASCII文字を1列に描画
	for (int i = 0, char c = '!'; c <= '~'; ++c, ++i) { WriteAscii(*pixel_writer, 8 * i, 50, c, { 0, 0, 0 }); }

	//新規コンソール
	console = new(console_buf) Console{ *pixel_writer, {0, 0, 0}, {255, 255, 255} };

	//printkを用いたコンソール表示
	for (int i = 0; i < 27; ++i) { printk("printk: %d\n", i); }

	while (1) __asm__("hlt");
}