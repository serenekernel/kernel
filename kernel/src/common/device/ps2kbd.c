#include <arch/hardware/lapic.h>
#include <common/irql.h>
#include <common/sched/sched.h>
#include <common/tty.h>
#include <device/ps2kbd.h>
#include <linked_list.h>
#include <memory/heap.h>
#include <spinlock.h>

#include "arch/interrupts.h"
#include "arch/io.h"
#include "common/dpc.h"

#define I8042_DATA_PORT 0x60
#define I8042_CMD_PORT 0x64

#define SCANCODE_RING_BUFFER_SIZE 256

uint8_t scancode_ring_buffer[SCANCODE_RING_BUFFER_SIZE];
size_t scancode_ring_buffer_head = 0;
size_t scancode_ring_buffer_tail = 0;

#define ESC (0x1B)
#define BS (0x08)

const uint8_t lower_ascii_codes[256] = { 0x00, ESC,  '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',  '9', '0',  '-',  '=', BS,  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u',  'i',  'o',  'p',  '[',  ']',  '\n', 0x00, 'a',
                                         's',  'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',  '\'', '`', 0x00, '\\', 'z', 'x', 'c',  'v', 'b', 'n', 'm', ',', '.', '/',  0x00, '*',  0x00, ' ',  0x00, 0x00, 0x00, 0x00,
                                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, '7',  '8', '9',  '-',  '4', '5', '6',  '+', '1', '2', '3', '0', '.', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

const uint8_t upper_ascii_codes[256] = { 0x00, ESC,  '!',  '@',  '#',  '$',  '%',  '^',  '&',  '*', '(', ')',  '_', '+', BS,  '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U',  'I',  'O',  'P',  '{',  '}',  '\n', 0x00, 'A',
                                         'S',  'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',  '"', '~', 0x00, '|', 'Z', 'X', 'C',  'V', 'B', 'N', 'M', '<', '>', '?',  0x00, '*',  0x00, ' ',  0x00, 0x00, 0x00, 0x00,
                                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, '7', '8', '9',  '-', '4', '5', '6',  '+', '1', '2', '3', '0', '.', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
#define KEY_LSHIFT_PRESSED 0x2A
#define KEY_LSHIFT_RELEASED 0xAA

#define KEY_RSHIFT_PRESSED 0x36
#define KEY_RSHIFT_RELEASED 0xB6

#define KEY_LCTRL_PRESSED 0x1D
#define KEY_LCTRL_RELEASED 0x9D

bool lshift_pressed = false;
bool rshift_pressed = false;

bool lctrl_pressed = false;
bool rctrl_pressed = false;

dpc_t* ps2kbd_dpc_object;

char translate_next_scancode() {
    if(scancode_ring_buffer_tail == scancode_ring_buffer_head) { return 0; }
    bool prefixed = false;
    uint8_t scancode = scancode_ring_buffer[scancode_ring_buffer_tail];
    if(scancode == 0xE0) {
        prefixed = true;
        size_t next_index = (scancode_ring_buffer_tail + 1) % SCANCODE_RING_BUFFER_SIZE;
        if(next_index == scancode_ring_buffer_head) { return 0; }
        scancode = scancode_ring_buffer[scancode_ring_buffer_tail];
        scancode_ring_buffer_tail = next_index;
    }

    scancode_ring_buffer_tail = (scancode_ring_buffer_tail + 1) % SCANCODE_RING_BUFFER_SIZE;

    if(!prefixed && scancode == KEY_LSHIFT_PRESSED) {
        lshift_pressed = true;
    } else if(!prefixed && scancode == KEY_LSHIFT_RELEASED) {
        lshift_pressed = false;
    } else if(!prefixed && scancode == KEY_RSHIFT_PRESSED) {
        rshift_pressed = true;
    } else if(!prefixed && scancode == KEY_RSHIFT_RELEASED) {
        rshift_pressed = false;
    } else if(!prefixed && scancode == KEY_LCTRL_PRESSED) {
        lctrl_pressed = true;
    } else if(!prefixed && scancode == KEY_LCTRL_RELEASED) {
        lctrl_pressed = false;
    } else if(prefixed && scancode == KEY_LCTRL_PRESSED) {
        rctrl_pressed = true;
    } else if(prefixed && scancode == KEY_LCTRL_RELEASED) {
        rctrl_pressed = false;
    }

    if(lctrl_pressed || rctrl_pressed) {
        return lower_ascii_codes[scancode] & 0x1f;
    } else if(lshift_pressed || rshift_pressed) {
        return upper_ascii_codes[scancode];
    } else {
        return lower_ascii_codes[scancode];
    }
}

void ps2kbd_dpc(dpc_t* dpc, void* arg) {
    (void) dpc;
    (void) arg;
    if(!g_tty) {
        scancode_ring_buffer_tail = scancode_ring_buffer_head;
        return;
    }

    while(scancode_ring_buffer_tail != scancode_ring_buffer_head) {
        int translated = translate_next_scancode();
        if(translated == 0) break;
        tty_put_byte(g_tty, translated);
    }
}

void ps2kbd_interrupt_handler(interrupt_frame_t* frame) {
    (void) frame;
    uint8_t scancode = port_read_u8(I8042_DATA_PORT);
    scancode_ring_buffer[scancode_ring_buffer_head] = scancode;
    scancode_ring_buffer_head = (scancode_ring_buffer_head + 1) % SCANCODE_RING_BUFFER_SIZE;
    if(!ps2kbd_dpc_object->in_use) dpc_enqueue(ps2kbd_dpc_object, NULL);
}

void ps2kbd_init() {
    ps2kbd_dpc_object = dpc_create(ps2kbd_dpc, false);

    interrupts_register_handler(0x21, ps2kbd_interrupt_handler);
    interrupts_route_irq(1, 0x21);
    interrupts_unmask_irq(1);

    while(true) {
        uint8_t status = port_read_u8(I8042_CMD_PORT);
        if(status & 0x1) {
            port_read_u8(I8042_DATA_PORT);
        } else {
            break;
        }
    }
}
