#include "console.hpp"

//コンストラクタ定義
Console::Console(PixelWriter& writer, const PixelColor& fg_col, const PixelColor& bg_col) : writer_{ writer }, fg_col_{ fg_col }, bg_col_{ bg_col }, buffer_{}, cursor_row_{ 0 }, cursor_column_{ 0 } {}
//コンストラクタ定義終了

//改行
void Console::Newline() {
	cursor_column_ = 0;
	if (cursor_row_ < kRows - 1) {
		++cursor_row_;
	}
	else {
		for (int y = 0; y < 16 * kRows; ++y) {
			for (int x = 0; x < 8 * kColumns; ++x) { writer_.write(x, y, bg_col_); }
		}

		for (int row = 0; row < kROws - 1; ++row) {
			memcpy(buffer_[row], buffer_[row + 1], kColumns + 1);
			WriteString(writer_, 0, 16 * row, buffer_[row], fg_col_);
		}
		memset(buffer_[kRows - 1], 0, kColumns + 1);
	}
}
//改行終了

void Concole::PutString(const char* s) {
	while (*s) {
		if (*s == '\n') {
			Newline();
		}
		else if (cursor_column < kColumns - 1) {
			WriteAscii(writer_, 8 * cursor_column, 16 * cursor_row_, *s, fg_col_);
			buffer_[cursor_row_][cursor_column_] = *s;
			++cursor_column_;
		}
		++s;
	}
}