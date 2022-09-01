//calc
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <tuple>
#include "../syscall.h"

char buttons[] = { "0","1","2","3","4","5","6","7","8","9","+","-","*","/" };

extern "C" void main(int argc, char** argv) {
	
	SyscallResult window = SyscallOpenWindow(100, 200, 10, 10, "calc");

	for (int i = 0; i < strlen(buttons); ++i) {

	}

	SyscallWinRedraw(layer_id);
	WaitEvent();

	SyscallCloseWindow(layer_id);
	exit(0);
}