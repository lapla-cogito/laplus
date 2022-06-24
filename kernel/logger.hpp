/**
 * @file logger.hpp
 *
 * カーネルロガー
 */
#pragma once

enum LogLevel {
	kError = 3,
	kWarn = 4,
	kInfo = 6,
	kDebug = 7,
};

//グローバルなログ優先度の閾値
void SetLogLevel(enum LogLevel level);

//ログを記録.指定の優先度に基づく.
int Log(enum LogLevel level, const char* format, ...)
__attribute__((format(printf, 2, 3)));