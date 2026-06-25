#include "App.hpp"

unsigned int _newlib_heap_size_user = 192 * 1024 * 1024;
unsigned int _pthread_stack_default_user = 2 * 1024 * 1024;

int main() {
    return appMain(0, nullptr);
}
