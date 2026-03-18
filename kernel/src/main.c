#include <common/arch.h>
#include <common/requests.h>
#include <stdio.h>

void kmain(void) {
    verify_requests();
    term_init();

    arch_init_bsp();
    arch_die();
}
