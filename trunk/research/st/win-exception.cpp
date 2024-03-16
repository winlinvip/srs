/*
Directly compile c++ source and execute:
    g++ win-exception.cpp ../../objs/st/libst.a -g -O0 -o win-exception && ./win-exception

Compile to assembly and then compile assembly to executable:
    gcc -S -masm=intel win-exception.cpp -o win-exception.s
    g++ win-exception.s ../../objs/st/libst.a -g -O0 -o win-exception && ./win-exception
*/
#include <stdio.h>
#include <exception>
#include "../../objs/st/st.h"

int handle_exception() {
    try {
        throw 3;
    } catch (...) {
        return 5;
    }
}

void* foo(void* arg) {
    int r0 = handle_exception();
    printf("r0=%d\n", r0);
    return NULL;
}

void* pfn(void* arg) {
    st_thread_create(foo, NULL, 0, 0);
    return NULL;
}

int main(int argc, char** argv) {
    st_init();
    if (argc > 2) {
        foo(NULL);
    } else if (argc > 1) {
        st_thread_create(pfn, NULL, 0, 10 * 1024 * 1024);
    } else {
        st_thread_create(foo, NULL, 0, 10 * 1024 * 1024);
    }
    st_thread_exit(NULL);
    return 0;
}

