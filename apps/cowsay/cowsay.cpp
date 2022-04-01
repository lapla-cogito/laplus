//cowsayコマンド

#include <cstdio>
#include <cstdlib>
#include <string>
#include "../syscall.h"

using namespace std;

//うし
char ushi[] = "        \\   ^__^\n         \\  (oo)\\\n            (__)\\       )\\/\\\n                ||----w |\n                ||     ||\n";

extern "C" void main(int argc, char** argv) {
	if (argc < 1) {
		fprintf(stderr, "Usage: %s <string>\n", argv[0]);
		exit(1);
	}

	char str[] = "";

	//スペースが入っていた時用にconcat処理
	if (argc == 1) { strcpy(str, argv[1]); }
	else {
		for (int i = 1; i < argc; ++i) {
			strcat(str, argv[i]);
			if (i != argc - 1) { strcat(str, " "); }
		}
	}

	//from here, output cowsay
	printf(" ");
	for (int i = 0; i < str.length() + 2; ++i) { printf("_"); }
	printf(" \n< %s >\n ", str);

	for (int i = 0; i < str.length() + 2; ++i) { printf("-"); }
	printf(" \n%s", ushi);
}