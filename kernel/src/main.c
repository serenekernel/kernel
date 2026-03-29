#include <common/arch.h>
#include <common/requests.h>
#include <stdio.h>

void kmain(void) {
    verify_requests();
    arch_init_bsp();
    arch_die();
}
