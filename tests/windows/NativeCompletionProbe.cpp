/* SPDX-License-Identifier: MIT */
#include <cstdio>
#include <cstring>
int main(int argc, char **argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--help") == 0) {
        std::puts("Usage: new-command [options]\n  --alpha  First option\n  --beta   Second option\n  -S -s    Distinct case-sensitive switches");
        return 0;
    }
    std::puts("UNEXPECTED_EXECUTION"); return 3;
}
