#include "greet.h"

/* Greets everyone named on the command line, or the world when nobody is:
 * what `build run -- Ada Alan` hands over ends up here. */
int main(int argc, char **argv) {
    if (argc < 2) greet("world");
    for (int i = 1; i < argc; i++) greet(argv[i]);
    return 0;
}
