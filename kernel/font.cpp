#include "font.hpp"

//hankaku.oのデータを参照して変数に
extern const uint8_t _binary_hankaku_bin_start;
extern const uint8_t _binary_hankaku_bin_end;
extern const uint8_t _binary_hankaku_bin_size;

const uint8_t* GetFont(char c) {
	auto index = 16 * static_cast<unsigned int>(c);
	if (index >= reinterpret_cast<uintptr_t>(&_binary_hankaku_bin_size)) { return nullptr; }
	return &_binary_hankaku_bin_start + index;
}
//参照終了

//文字描画
void WriteAscii(PixelWriter& writer, int x, int y, char c, const PixelColor& col) {
	const uint8_t* font = GetFont(c);
	if (font == nullptr) { return; }
	for (int dy = 0; dy < 16; ++dy) {
		for (int dx = 0; dx < 8; ++dx) {
			if ((font[dy] << dx) & 0x80u) { writer.Write(x + dx, y + dy, col); }
		}
	}
}
//ASCII描画終了

//文字列描画
void WriteString(PixelWriter& writer, int x, int y, const char* s, const PixelColor& col) {
	for (int i = 0; i < s[i] != '\0'; ++i) { WriteAscii(writer, i * 8 + x, y, s[i], col); }
}
//文字列描画終了