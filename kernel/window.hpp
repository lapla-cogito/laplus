//Windowクラスがグラフィックの表示領域を表すクラス
#pragma once
#include <vector>
#include <optional>
#include <string>
#include "graphics.hpp"
#include "frame_buffer.hpp"

enum class WindowRegion {
	kTitleBar,
	kCloseButton,
	kBorder,
	kOther,
};

class Window {
public:
	//Windowと関連付けられたPixelWriter
	class WindowWriter : public PixelWriter {
	public:
		WindowWriter(Window& window) : window_{ window } {}
		//指定された位置に指定された色のピクセルを描く
		virtual void Write(Vector2D<int> pos, const PixelColor& c) override { window_.Write(pos, c); }
		//Width()は関連付けられたWindowの横幅をピクセル単位で返す
		virtual int Width() const override { return window_.Width(); }
		//Height()は関連付けられたWindowの高さをピクセル単位で返す
		virtual int Height() const override { return window_.Height(); }

	private:
		Window& window_;
	};

	//指定されたピクセル数の平面描画領域を作成
	Window(int width, int height, PixelFormat shadow_format);
	virtual ~Window() = default;
	Window(const Window& rhs) = delete;
	Window& operator=(const Window& rhs) = delete;

	//与えられたFrameBufferにウィンドウの表示領域を描画する.dstは描画先,posはdstの左上を基準としたウィンドウの位置,areaはdstの左上を基準とした描画対象範囲
	void DrawTo(FrameBuffer& dst, Vector2D<int> pos, const Rectangle<int>& area);
	//透過色を設定する
	void SetTransparentColor(std::optional<PixelColor> c);
	//このインスタンスに紐付いたWindowWriterを取得する
	WindowWriter* Writer();

	//指定した位置のピクセルを返す
	const PixelColor& At(Vector2D<int> pos) const;
	//指定した位置にピクセルを書き込む
	void Write(Vector2D<int> pos, PixelColor c);

	//平面描画領域の横幅をピクセル単位で返す
	int Width() const;
	//平面描画領域の高さをピクセル単位で返す
	int Height() const;
	//平面描画領域のサイズをピクセル単位で返す
	Vector2D<int> Size() const;

	//ウィンドウの平面描画領域内で矩形領域を移動.src_posは移動元矩形の原点,src_sizeは移動元矩形の大きさ,dst_posは移動先の原点
	void Move(Vector2D<int> dst_pos, const Rectangle<int>& src);

	virtual void Activate() {}
	virtual void Deactivate() {}
	virtual WindowRegion GetWindowRegion(Vector2D<int> pos);

private:
	int width_, height_;
	std::vector<std::vector<PixelColor>> data_{};
	WindowWriter writer_{ *this };
	std::optional<PixelColor> transparent_color_{ std::nullopt };

	FrameBuffer shadow_buffer_{};
};

class ToplevelWindow : public Window {
public:
	static constexpr Vector2D<int> kTopLeftMargin{ 4, 24 };
	static constexpr Vector2D<int> kBottomRightMargin{ 4, 4 };
	static constexpr int kMarginX = kTopLeftMargin.x + kBottomRightMargin.x;
	static constexpr int kMarginY = kTopLeftMargin.y + kBottomRightMargin.y;

	class InnerAreaWriter : public PixelWriter {
	public:
		InnerAreaWriter(ToplevelWindow& window) : window_{ window } {}

		virtual void Write(Vector2D<int> pos, const PixelColor& c) override { window_.Write(pos + kTopLeftMargin, c); }
		virtual int Width() const override { return window_.Width() - kTopLeftMargin.x - kBottomRightMargin.x; }
		virtual int Height() const override { return window_.Height() - kTopLeftMargin.y - kBottomRightMargin.y; }

	private:
		ToplevelWindow& window_;
	};

	ToplevelWindow(int width, int height, PixelFormat shadow_format,
		const std::string& title);

	virtual void Activate() override;
	virtual void Deactivate() override;
	virtual WindowRegion GetWindowRegion(Vector2D<int> pos) override;

	InnerAreaWriter* InnerWriter() { return &inner_writer_; }
	Vector2D<int> InnerSize() const;

private:
	std::string title_;
	InnerAreaWriter inner_writer_{ *this };
};

void DrawWindow(PixelWriter& writer, const char* title);
void DrawTextbox(PixelWriter& writer, Vector2D<int> pos, Vector2D<int> size);
void DrawTerminal(PixelWriter& writer, Vector2D<int> pos, Vector2D<int> size);
void DrawWindowTitle(PixelWriter& writer, const char* title, bool active);