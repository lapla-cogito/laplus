#include "graphics.hpp"

//RGB
void RGBResv8BitPerColorPixelWriter::Write(int x, int y, const PixelColor& c) {
	auto p = PixelAt(x, y);
	p[0] = c.r;
	p[1] = c.g;
	p[2] = c.b;
}
//RGB終了


//BGR
void BGRResv8BitPerColorPixelWriter::Write(int x, int y, const PixelColor& c) {
	auto p = PixelAt(x, y);
	p[0] = c.b;
	p[1] = c.g;
	p[2] = c.r;
}
//BGR終了