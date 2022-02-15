#pragma once
#include "graphics.hpp"

class Concole {
private:
	void Newline();
	PixelWriter& writer_;
	const PixelColor fg_col_, bg_col_;
	char buffer_[kRows][kColumns + 1];
	int cursor_row_, cursor_column;

public:
	static const int kRows = 25, kColumns = 80;
	Console(PixelWriter& writer, const PixelColor& fg_col, const PixelColor& bg_col);
	void PutString(conast char* s);
};