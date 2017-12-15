#pragma once
/*
    kbd.h -- keyboard matrix helpers

    A keyboard_matrix instance maps key codes to the cross-sections of
    an up to 12x12 keyboard matrix with up to 4 modifier keys (shift, ctrl, ...)

        C0  C1  C1  C2  C3 ... C12
         /   /   /   /   /       |
    L0---+---+---+---+---+-....--+
         |   /   /   /   /       /
    L1---+---+---+---+---+-....--+
         .   .   .   .   .       .
    L12--+---+---+---+---+-....--+

    First register host-system key codes with the keyboard matrix (where are keys
    positioned in the matrix) by calling kbd_register_modifier() and
    kbd_register_key().

    Feed 'host system key presses' into the keyboard_matrix instance
    by calling kbd_key_down() and kbd_key_up(). Some emulated systems
    took quite long to scan a key press, so the keyboard matrix has
    a 'sticky count', which may prolong the key press visible to the
    emulated system if the host-system key press was too short.

    Call the function kbd_test_lines() to check the current state of the
    keyboard matrix.
*/
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KBD_MAX_COLUMNS (12)
#define KBD_MAX_LINES (12)
#define KBD_MAX_MOD_KEYS (4)
#define KBD_MAX_KEYS (256)
#define KBD_MAX_PRESSED_KEYS (4)

/* desc structure for kbd_setup() */
typedef struct {
    /* the number of kbd_update() ticks a key will at least remain down, default is 2 */
    int sticky_count;
} kbd_desc_t;

/* a pressed-key state */
typedef struct {
    /* key code of the pressed key */
    int key;
    /* mask bit layout is 8-bits modifier, and 12-bits each columns and lines */
    /* |SSSSSSSS|CCCCCCCCCCCC|LLLLLLLLLLLL| */
    uint32_t mask;
    /* the frame-count when the key was pressed down */
    uint32_t pressed_frame;
    /* the frame-count when the key was released, 0 if not yet released */
    uint32_t released_frame;
} key_state_t;

/* keyboard matrix state */
typedef struct {
    /* current frame counter, bumped by kbd_update() */
    uint32_t frame_count;
    /* number of frames a key will at least remain pressed */
    int sticky_count;
    /* currently active columns */
    uint16_t active_columns;
    /* currently active lines */
    uint16_t active_lines;
    /* map key ASCII code to modifier/column/line bits */
    uint32_t key_masks[KBD_MAX_KEYS];
    /* column/line bits for modifier keys */
    uint32_t mod_masks[KBD_MAX_MOD_KEYS];
    /* currently pressed keys (bitmask==0 is empty slot) */
    key_state_t key_buffer[KBD_MAX_PRESSED_KEYS];
} kbd_t;

/* initialize a keyboard matrix instance */
extern void kbd_init(kbd_t* kbd, kbd_desc_t* desc);
/* update keyboard matrix state (releases sticky keys), usually call once per frame */
extern void kbd_update(kbd_t* kbd);
/* register a modifier key, layers are between from 0 to KBD_MAX_MOD_KEYS-1 */
extern void kbd_register_modifier(kbd_t* kbd, int layer, int column, int line);
/* register a key */
extern void kbd_register_key(kbd_t* kbd, int key, int column, int line, int mod_mask);
/* add a key to the pressed-key buffer */
extern void kbd_key_down(kbd_t* kbd, int key);
/* remove a key from the pressed-key buffer */
extern void kbd_key_up(kbd_t* kbd, int key);
/* activate columns and return which lines are lit */
extern uint16_t kbd_test_lines(kbd_t* kbd, uint16_t column_mask);
/* set active column mask (use together with kbd_scan_lines */
extern void kbd_set_active_columns(kbd_t* kbd, uint16_t column_mask);
/* scan active lines (used together with kbd_set_active_columns */
extern uint16_t kbd_scan_lines(kbd_t* kbd);
/* set active lines mask (use together with kbd_scan_columns */
extern void kbd_set_active_lines(kbd_t* kbd, uint16_t line_mask);
/* scan active columns (used together with kbd_set_active_lines */
extern uint16_t kbd_scan_columns(kbd_t* kbd);

/*-- IMPLEMENTATION ----------------------------------------------------------*/
#ifdef CHIPS_IMPL
#include <string.h>
#ifndef CHIPS_DEBUG
    #ifdef _DEBUG
        #define CHIPS_DEBUG
    #endif
#endif
#ifndef CHIPS_ASSERT
    #include <assert.h>
    #define CHIPS_ASSERT(c) assert(c)
#endif

void kbd_init(kbd_t* kbd, kbd_desc_t* desc) {
    CHIPS_ASSERT(kbd && desc);
    memset(kbd, 0, sizeof(*kbd));
    kbd->frame_count = 1;
    kbd->sticky_count = (desc->sticky_count == 0) ? 2 : desc->sticky_count;
}

void kbd_update(kbd_t* kbd) {
    CHIPS_ASSERT(kbd);
    kbd->frame_count++;
    /* check for sticky keys that should be released */
    for (int i = 0; i < KBD_MAX_PRESSED_KEYS; i++) {
        key_state_t* k = &kbd->key_buffer[i];
        if (k->released_frame != 0) {
            if (kbd->frame_count > (k->pressed_frame + kbd->sticky_count)) {
                k->mask = 0;
                k->key = 0;
                k->pressed_frame = 0;
                k->released_frame = 0;
            }
        }
    }
}

void kbd_register_modifier(kbd_t* kbd, int layer, int column, int line) {
    CHIPS_ASSERT(kbd);
    CHIPS_ASSERT((column >= 0) && (column < KBD_MAX_COLUMNS));
    CHIPS_ASSERT((line >= 0) && (line < KBD_MAX_LINES));
    CHIPS_ASSERT((layer >= 0) && (layer < KBD_MAX_MOD_KEYS));
    kbd->mod_masks[layer] = (1<<(layer+KBD_MAX_COLUMNS+KBD_MAX_LINES)) | (1<<(column+KBD_MAX_COLUMNS)) | (1<<line);
}

void kbd_register_key(kbd_t* kbd, int key, int column, int line, int mod_mask) {
    CHIPS_ASSERT(kbd);
    CHIPS_ASSERT((key >= 0) && (key < KBD_MAX_KEYS));
    CHIPS_ASSERT((column >= 0) && (column < KBD_MAX_COLUMNS));
    CHIPS_ASSERT((line >= 0) && (line < KBD_MAX_LINES));
    kbd->key_masks[key] = (mod_mask << (KBD_MAX_COLUMNS+KBD_MAX_LINES)) | (1<<(column+KBD_MAX_COLUMNS)) | (1<<line);
}

void kbd_key_down(kbd_t* kbd, int key) {
    CHIPS_ASSERT(kbd && (key >= 0) && (key < KBD_MAX_KEYS));
    /* find a free keybuffer slot */
    for (int i = 0; i < KBD_MAX_PRESSED_KEYS; i++) {
        key_state_t* k = &kbd->key_buffer[i];
        if (0 == k->mask) {
            k->key = key;
            k->mask = kbd->key_masks[key];
            k->pressed_frame = kbd->frame_count;
            k->released_frame = 0;
            return;
        }
    }
}

void kbd_key_up(kbd_t* kbd, int key) {
    CHIPS_ASSERT(kbd && (key >= 0) && (key < KBD_MAX_KEYS));
    /* find the key in the keybuffer, just set released_frame */
    for (int i = 0; i < KBD_MAX_PRESSED_KEYS; i++) {
        key_state_t* k = &kbd->key_buffer[i];
        if (key == k->key) {
            k->released_frame = kbd->frame_count;
        }
    }
}

/* extract column bits from a 32-bit key mask */
static uint16_t _kbd_columns(uint32_t key_mask) {
    return (key_mask>>KBD_MAX_COLUMNS) & ((1<<KBD_MAX_COLUMNS)-1);
}

/* extract line bits from a 32-bit key mask */
static uint16_t _kbd_lines(uint32_t key_mask) {
    return key_mask & ((1<<KBD_MAX_LINES)-1);
}

/* extract modifier mask bits from a 32-bit key mask */
static uint32_t _kbd_mod(uint32_t key_mask) {
    return key_mask & ((1<<KBD_MAX_MOD_KEYS)-1)<<(KBD_MAX_COLUMNS+KBD_MAX_LINES);
}

/* scan keyboard matrix lines by column mask */
uint16_t kbd_test_lines(kbd_t* kbd, uint16_t column_mask) {
    CHIPS_ASSERT(kbd);
    uint16_t line_bits = 0;
    for (int key_index = 0; key_index < KBD_MAX_PRESSED_KEYS; key_index++) {
        const uint32_t key_mask = kbd->key_buffer[key_index].mask;
        if (key_mask) {
            const uint16_t key_col_mask = _kbd_columns(key_mask);
            if ((key_col_mask & column_mask) == key_col_mask) {
                line_bits |= _kbd_lines(key_mask);
            }
            const uint32_t key_mod_mask = _kbd_mod(key_mask);
            if (key_mod_mask) {
                for (int mod_index = 0; mod_index < KBD_MAX_MOD_KEYS; mod_index++) {
                    const uint32_t mod_mask = kbd->mod_masks[mod_index];
                    if (mod_mask & key_mod_mask) {
                        const uint16_t mod_col_mask = _kbd_columns(mod_mask);
                        if ((mod_col_mask & column_mask) == (mod_col_mask)) {
                            line_bits |= _kbd_lines(mod_mask);
                        }
                    }
                }
            }
        }
    }
    return line_bits;
}

/* scan keyboard matrix lines by column mask */
uint16_t kbd_test_columns(kbd_t* kbd, uint16_t line_mask) {
    CHIPS_ASSERT(kbd);
    uint16_t column_bits = 0;
    for (int key_index = 0; key_index < KBD_MAX_PRESSED_KEYS; key_index++) {
        const uint32_t key_mask = kbd->key_buffer[key_index].mask;
        if (key_mask) {
            const uint16_t key_line_mask = _kbd_lines(key_mask);
            if ((key_line_mask & line_mask) == key_line_mask) {
                column_bits |= _kbd_columns(key_mask);
            }
            const uint32_t key_mod_mask = _kbd_mod(key_mask);
            if (key_mod_mask) {
                for (int mod_index = 0; mod_index < KBD_MAX_MOD_KEYS; mod_index++) {
                    const uint32_t mod_mask = kbd->mod_masks[mod_index];
                    if (mod_mask & key_mod_mask) {
                        const uint16_t mod_line_mask = _kbd_lines(mod_mask);
                        if ((mod_line_mask & line_mask) == (mod_line_mask)) {
                            column_bits |= _kbd_columns(mod_mask);
                        }
                    }
                }
            }
        }
    }
    return column_bits;
}

/* set currently active columns */
void kbd_set_active_columns(kbd_t* kbd, uint16_t column_mask) {
    kbd->active_columns = column_mask;
}

/* scan the keyboard matrix using currently active columns */
uint16_t kbd_scan_lines(kbd_t* kbd) {
    return kbd_test_lines(kbd, kbd->active_columns);
}

/* set currently active lines */
void kbd_set_active_lines(kbd_t* kbd, uint16_t line_mask) {
    kbd->active_lines = line_mask;
}

/* scan the keyboard matrix using currently active lines */
uint16_t kbd_scan_columns(kbd_t* kbd) {
    return kbd_test_columns(kbd, kbd->active_lines);
}

#endif /* CHIPS_IMPL */

#ifdef __cplusplus
} /* extern "C" */
#endif