//画像描画用
#include "graphics.hpp"
#include "../apps/gviewer/stb_image.h"

void RGBResv8BitPerColorPixelWriter::Write(Vector2D<int> pos, const PixelColor& c) {
	auto p = PixelAt(pos);
	p[0] = c.r;
	p[1] = c.g;
	p[2] = c.b;
}

void BGRResv8BitPerColorPixelWriter::Write(Vector2D<int> pos, const PixelColor& c) {
	auto p = PixelAt(pos);
	p[0] = c.b;
	p[1] = c.g;
	p[2] = c.r;
}

uint32_t GetColorRGB(unsigned char* image_data) {
	return static_cast<uint32_t>(image_data[0]) << 16 |
		static_cast<uint32_t>(image_data[1]) << 8 |
		static_cast<uint32_t>(image_data[2]);
}

void DrawRectangle(PixelWriter& writer, const Vector2D<int>& pos,
	const Vector2D<int>& size, const PixelColor& c) {
	for (int dx = 0; dx < size.x; ++dx) {
		writer.Write(pos + Vector2D<int>{dx, 0}, c);
		writer.Write(pos + Vector2D<int>{dx, size.y - 1}, c);
	}
	for (int dy = 1; dy < size.y - 1; ++dy) {
		writer.Write(pos + Vector2D<int>{0, dy}, c);
		writer.Write(pos + Vector2D<int>{size.x - 1, dy}, c);
	}
}

void FillRectangle(PixelWriter& writer, const Vector2D<int>& pos,
	const Vector2D<int>& size, const PixelColor& c) {
	for (int dy = 0; dy < size.y; ++dy) {
		for (int dx = 0; dx < size.x; ++dx) {
			writer.Write(pos + Vector2D<int>{dx, dy}, c);
		}
	}
}

std::tuple<int, uint8_t*, size_t> MapFile(const char* filepath) {
	SyscallResult res = SyscallOpenFile(filepath, O_RDONLY);
	if (res.error) {
		fprintf(stderr, "%s: %s\n", strerror(res.error), filepath);
		exit(1);
	}

	const int fd = res.value;
	size_t filesize;
	res = SyscallMapFile(fd, &filesize, 0);
	if (res.error) {
		fprintf(stderr, "%s\n", strerror(res.error));
		exit(1);
	}

	return { fd, reinterpret_cast<uint8_t*>(res.value), filesize };
}

//デスクトップ描画
void DrawDesktop(PixelWriter& writer) {
	const auto width = writer.Width();
	const auto height = writer.Height();
	/*FillRectangle(writer,
		{ 0, 0 },
		{ width, height - 50 },
		kDesktopBGColor);*/

		//壁紙描画
	int width, height, bytes_per_pixel;
	const char* filepath = "wallpaper.png";
	const auto [fd, content, filesize] = MapFile(filepath);

	unsigned char* image_data = stbi_load_from_memory(
		content, filesize, &width, &height, &bytes_per_pixel, 0);
	//wallpaper.pngを読み込めなかったらデフォのfillrect
	if (image_data == nullptr) {
		//fprintf(stderr, "Failed to load image: %s\n", stbi_failure_reason());
		FillRectangle(writer,
			{ 0, 0 },
			{ width, height - 50 },
			kDesktopBGColor);
	}
	else {
		auto get_color = GetColorRGB;
		if (bytes_per_pixel <= 2) { get_color = GetColorGray; }

		const char* last_slash = strrchr(filepath, '/');
		const char* filename = last_slash ? &last_slash[1] : filepath;

		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				uint32_t c = get_color(&image_data[bytes_per_pixel * (y * width + x)]);
				FillRectangle(writer,
					{ x - 1,y - 1 },
					{ x,y },
					ToColor(c));
			}
		}
	}
	//壁紙描画終了

	FillRectangle(writer,
		{ 0, height - 50 },
		{ width, 50 },
		{ 1, 8, 17 });
	FillRectangle(writer,
		{ 0, height - 50 },
		{ width / 5, 50 },
		{ 80, 80, 80 });
	DrawRectangle(writer,
		{ 10, height - 40 },
		{ 30, 30 },
		{ 160, 160, 160 });
}

FrameBufferConfig screen_config;
PixelWriter* screen_writer;

Vector2D<int> ScreenSize() {
	return {
	  static_cast<int>(screen_config.horizontal_resolution),
	  static_cast<int>(screen_config.vertical_resolution)
	};
}

namespace {
	char pixel_writer_buf[sizeof(RGBResv8BitPerColorPixelWriter)];
}

void InitializeGraphics(const FrameBufferConfig& screen_config) {
	::screen_config = screen_config;

	switch (screen_config.pixel_format) {
	case kPixelRGBResv8BitPerColor:
		::screen_writer = new(pixel_writer_buf)
			RGBResv8BitPerColorPixelWriter{ screen_config };
		break;
	case kPixelBGRResv8BitPerColor:
		::screen_writer = new(pixel_writer_buf)
			BGRResv8BitPerColorPixelWriter{ screen_config };
		break;
	default:
		exit(1);
	}

	DrawDesktop(*screen_writer);
}
