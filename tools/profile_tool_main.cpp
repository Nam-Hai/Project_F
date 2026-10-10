#include <iostream>

#include "profile_tool.h"

int main(int argc, char** argv) {
    return profile_tool::run(argc, argv, std::cout, std::cerr);
}
