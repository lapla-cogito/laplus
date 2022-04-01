//cowsayコマンド
#include <cstdio>
#include <cstdlib>
#include <string>

//うし
std::string ushi = "        \\   ^__^\n         \\  (oo)\\\n            (__)\\       )\\/\\\n                ||----w |\n                ||     ||\n";

extern "C" void main(int argc, char** argv) {
	if (argc < 1) {
		fprintf(stderr, "Usage: %s <string>\n", argv[0]);
		exit(1);
	}

	std::string str = "";

	//スペースが入っていた時用にconcat処理
	if (argc == 1) { str = argv[1]; }
	else {
		for (int i = 1; i < argc; ++i) {
			str += argv[i];
			if (i != argc - 1) { str += " "; }
		}
	}

	//from here, output cowsay
	printf(" ");
	for (int i = 0; i < str.length() + 2; ++i) { printf("_"); }
	printf(" "); 
	puts();

	std::count << "< " << str << " >" << endl;

	printf(" ");
	for (int i = 0; i < str.length() + 2; ++i) { printf("-"); }
	printf(" "); 
	puts();
	cout << ushi;
}