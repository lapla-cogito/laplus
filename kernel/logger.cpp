#include <cstddef>
#include <cstdio>
#include "console.hpp"
#include "logger.hpp"

namespace {
	LogLevel log_level = kWarn;
}

extern Console* console;

//levelにログの優先度を記録.閾値以上のものだけ表示
void SetLogLevel(LogLevel level) {
	log_level = level;
}

//カーネルログ
int Log(LogLevel level, const char* format, ...) {
	if (level > log_level) { return 0; }

	va_list ap;
	int result;
	char s[1024];

	va_start(ap, format);
	result = vsprintf(s, format, ap);
	va_end(ap);

	console->PutString(s);
	return result;
}