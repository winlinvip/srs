/*
Directly compile c++ source and execute:
    g++ win-exception-min.cpp -g -O0 -o win-exception-min && ./win-exception-min

Compile to assembly and then compile assembly to executable:
    gcc -S -masm=intel win-exception-min.cpp -o win-exception-min.s
    g++ win-exception-min.s -g -O0 -o win-exception-min && ./win-exception-min
*/
#include <stdio.h>

int handle_exception() {
    try {
        throw 3;
    } catch (...) {
        return 5;
    }
}

int main(int argc, char** argv) {
    int r0 = handle_exception();
    printf("r0=%d\n", r0);
    return 0;
}
