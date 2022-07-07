#include <iostream>
#include <cstdlib>
#include <CXXException/StackTraceSaver.h>


void f1() {
    throw std::exception();
}

void f2() {
    for (int a = 0; a < 1; a++)
        f1();
}

void f3() {
    try {
        f2();
    } catch (std::exception &e) {
        auto st = CXXException::StackTraceSaver::instance()->retrieve(&e);
        std::cout << st->to_string() << std::endl;
    }
}

int main() {
    f3();

    return 0;
}
