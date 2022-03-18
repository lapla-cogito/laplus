//cowsayコマンド
#include <cstudio>
#include <cstdlib>


extern "C" void main(int argc, char** argv) {
	if (argc < 1) {
		fprintf(stderr, "Usage: %s <string>\n", argv[0]);
		exit(1);
	}


}