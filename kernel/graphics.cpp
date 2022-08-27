/**
 * @file graphics.cpp
 *
 * @brief 画面描画
 */
#include "graphics.hpp"
#include "app_event.hpp"
#include "asmfunc.h"
#include "fat.hpp"
#include "font.hpp"
#include "keyboard.hpp"
#include "logger.hpp"
#include "msr.hpp"
#include "syscall.hpp"
#include "task.hpp"
#include "terminal.hpp"
#include "timer.hpp"
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <fcntl.h>
//#include "appsyscall.h"

#define STBI_NO_THREAD_LOCALS
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "../apps/gviewer/stb_image.h"

void RGBResv8BitPerColorPixelWriter::Write(Vector2D<int> pos,
                                           const PixelColor &c) {
    auto p = PixelAt(pos);
    p[0] = c.r;
    p[1] = c.g;
    p[2] = c.b;
}

void BGRResv8BitPerColorPixelWriter::Write(Vector2D<int> pos,
                                           const PixelColor &c) {
    auto p = PixelAt(pos);
    p[0] = c.b;
    p[1] = c.g;
    p[2] = c.r;
}

uint32_t GetColorRGB(unsigned char *image_data) {
    return static_cast<uint32_t>(image_data[0]) << 16 |
           static_cast<uint32_t>(image_data[1]) << 8 |
           static_cast<uint32_t>(image_data[2]);
}

void DrawRectangle(PixelWriter &writer, const Vector2D<int> &pos,
                   const Vector2D<int> &size, const PixelColor &c) {
    for(int dx = 0; dx < size.x; ++dx) {
        writer.Write(pos + Vector2D<int>{dx, 0}, c);
        writer.Write(pos + Vector2D<int>{dx, size.y - 1}, c);
    }
    for(int dy = 1; dy < size.y - 1; ++dy) {
        writer.Write(pos + Vector2D<int>{0, dy}, c);
        writer.Write(pos + Vector2D<int>{size.x - 1, dy}, c);
    }
}

void FillRectangle(PixelWriter &writer, const Vector2D<int> &pos,
                   const Vector2D<int> &size, const PixelColor &c) {
    for(int dy = 0; dy < size.y; ++dy) {
        for(int dx = 0; dx < size.x; ++dx) {
            writer.Write(pos + Vector2D<int>{dx, dy}, c);
        }
    }
}

namespace {
size_t AllocateFD(Task &task) {
    const size_t num_files = task.Files().size();
    for(size_t i = 0; i < num_files; ++i) {
        if(!task.Files()[i]) { return i; }
    }
    task.Files().emplace_back();
    return num_files;
}

std::pair<fat::DirectoryEntry *, int> CreateFile(const char *path) {
    auto [file, err] = fat::CreateFile(path);
    switch(err.Cause()) {
    case Error::kIsDirectory:
        return {file, EISDIR};
    case Error::kNoSuchEntry:
        return {file, ENOENT};
    case Error::kNoEnoughMemory:
        return {file, ENOSPC};
    default:
        return {file, 0};
    }
}
} // namespace

std::pair<uint64_t, int> MakeMapFile(int n, size_t *t, int uku) {
    const int fd = n;
    size_t *file_size = reinterpret_cast<size_t *>(t);
    __asm__("cli");
    auto &task = task_manager->CurrentTask();
    __asm__("sti");

    if(fd < 0 || task.Files().size() <= fd || !task.Files()[fd]) {
        return {0, EBADF};
    }

    *file_size = task.Files()[fd]->Size();
    const uint64_t vaddr_end = task.FileMapEnd();
    const uint64_t vaddr_begin =
        (vaddr_end - *file_size) & 0xffff'ffff'ffff'f000;
    task.SetFileMapEnd(vaddr_begin);
    task.FileMaps().push_back(FileMapping{fd, vaddr_begin, vaddr_end});
    return {vaddr_begin, 0};
}

std::pair<size_t, int> OpenFile(const char *filepath, int flag) {
    const char *path = reinterpret_cast<const char *>(filepath);
    const int flags = flag;
    __asm__("cli");
    auto &task = task_manager->CurrentTask();
    __asm__("sti");

    if(strcmp(path, "@stdin") == 0) { return {0, 0}; }

    auto [file, post_slash] = fat::FindFile(path);
    if(file == nullptr) {
        if((flags & O_CREAT) == 0) { return {0, ENOENT}; }

        auto [new_file, err] = CreateFile(path);
        if(err) { return {0, err}; }

        file = new_file;
    } else if(file->attr != fat::Attribute::kDirectory && post_slash) {
        return {0, ENOENT};
    }

    size_t fd = AllocateFD(task);
    task.Files()[fd] = std::make_unique<fat::FileDescriptor>(*file);
    return {fd, 0};
}

std::tuple<int, uint8_t *, size_t> MapFile(const char *filepath) {
    // OpenFileはsize_tとerrのintのpairが返ってくる
    const auto [fd, err_of] = OpenFile(filepath, O_RDONLY);
    if(err_of) {
        fprintf(stderr, "%s: %s\n", strerror(err_of), filepath);
        exit(1);
    }

    size_t filesize;
    const auto [vaddr, err_mm] = MakeMapFile(fd, &filesize, 0);
    if(err_mm) {
        fprintf(stderr, "%s\n", strerror(err_mm));
        exit(1);
    }

    return {fd, reinterpret_cast<uint8_t *>(vaddr), filesize};
}

uint32_t GetColorGray(unsigned char *image_data) {
    const uint32_t gray = image_data[0];
    return gray << 16 | gray << 8 | gray;
}

//デスクトップ描画
void DrawDesktop(PixelWriter &writer) {
    const auto width = writer.Width();
    const auto height = writer.Height();
    FillRectangle(writer, {0, 0}, {width, height - 50}, kDesktopBGColor);

    //壁紙描画
    int imgwidth, imgheight, bytes_per_pixel;
    char wallpath[] = "wallpaper.png";
    const char *filepath = wallpath;
    const auto [fd, content, filesize] = MapFile(filepath);

    unsigned char *image_data = stbi_load_from_memory(
        content, filesize, &imgwidth, &imgheight, &bytes_per_pixel, 0);

    // wallpaper.pngを読み込めなかったらデフォのfillrectをする
    if(image_data == nullptr) {
        fprintf(stderr, "Failed to load image: %s\n", stbi_failure_reason());
        FillRectangle(writer, {0, 0}, {width, height - 50}, kDesktopBGColor);
    } else {
        auto get_color = GetColorRGB;
        if(bytes_per_pixel <= 2) { get_color = GetColorGray; }

        for(int y = 0; y < imgheight; ++y) {
            for(int x = 0; x < imgwidth; ++x) {
                uint32_t c = get_color(
                    &image_data[bytes_per_pixel * (y * imgwidth + x)]);
                FillRectangle(writer, {x - 1, y - 1}, {x, y}, ToColor(c));
            }
        }
    }
    //壁紙描画終了

    //タスクバーら辺
    FillRectangle(writer, {0, height - 50}, {width, 50}, {1, 8, 17});
    FillRectangle(writer, {0, height - 50}, {width / 5, 50}, {80, 80, 80});
    //左下のマーク
    DrawRectangle(writer, {10, height - 40}, {30, 30}, {160, 160, 160});
}

FrameBufferConfig screen_config;
PixelWriter *screen_writer;

Vector2D<int> ScreenSize() {
    return {static_cast<int>(screen_config.horizontal_resolution),
            static_cast<int>(screen_config.vertical_resolution)};
}

namespace {
char pixel_writer_buf[sizeof(RGBResv8BitPerColorPixelWriter)];
}

void InitializeGraphics(const FrameBufferConfig &screen_config) {
    ::screen_config = screen_config;

    switch(screen_config.pixel_format) {
    case kPixelRGBResv8BitPerColor:
        ::screen_writer =
            new(pixel_writer_buf) RGBResv8BitPerColorPixelWriter{screen_config};
        break;
    case kPixelBGRResv8BitPerColor:
        ::screen_writer =
            new(pixel_writer_buf) BGRResv8BitPerColorPixelWriter{screen_config};
        break;
    default:
        exit(1);
    }

    DrawDesktop(*screen_writer);
}
