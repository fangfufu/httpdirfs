#include "main.h"

#include "util.h"

int main(int argc, char **argv)
{
    CONFIG.sonic_mode = 1;
    return common_main(&argc, &argv);
}