#include "Host.h"

int main() {
    host::init();
    host::addStandardModes();
    return host::run();
}
