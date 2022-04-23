//エイリアス
#include <fstream>
#include <iostream>
#include <string>
#include <cstdio>
#include <cstdlib>

vector <std::string> split(const std::string & s, char delim) {
  vector <std::string> elems;
  std::stringstream ss(s);
  std::string item;
  while (getline(ss, item, delim)) {
    if (!item.empty()) {
      elems.push_back(item);
    }
  }
  return elems;
}

extern "C"
void main(int argc, char ** argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <command> <string>\n", argv[0]);
    exit(1);
  }

  if (argv[1] != "del") {
    // p.s.
    std::ofstream file("alias.txt", std::ios::app);

    if (!ofs) {
      std::cout << "An error has occured!" << std::endl;
      return 0;
    }

    for (int i = 0; i < argc - 1; ++i) {
      file << argv[i];
      if (i != argc - 2) {
        file << " ";
      }
      file << std::endl;
    }
    file.close();
  } else {
    ifstream in ("alias.txt");
    vector < std::string > lines, newlines;
    std::string line;

    if (! in ) {
      std::cout << "An error has occured!" << std::endl;
      return 0;
    }

    while (getline( in , line)) {
      lines.push_back(line);
    }

    for (auto i: lines) {
      auto res = split(i, " ");
      if (res[res.size() - 1] != argv[argc - 2]) {
        newlines.push_back(i);
      }
    }

    // overwrite
    std::ofstream file("alias.txt");

    for (int i = 0; i < newlines.size(); ++i) {
      file << newlines[i] << std::endl;
    }
  }
  return 0;
}