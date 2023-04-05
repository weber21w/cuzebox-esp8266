/*
 *  Keyboard Dongle Emulation
 *
 *  Copyright (C) 2016
 *    Sandor Zsuga (Jubatian)
 *  Uzem (the base of CUzeBox) is copyright (C)
 *    David Etherton,
 *    Eric Anderton,
 *    Alec Bourque (Uze),
 *    Filipe Rinaldi,
 *    Sandor Zsuga (Jubatian),
 *    Matt Pandina (Artcfox)
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

uint8 cu_kbd_state;
uint8 cu_kbd_clock;
uint8 cu_kbd_data_in;
uint8 cu_kbd_data_out;
uint8 cu_kbd_enabled;
uint8 cu_kbd_bypassed; /* don't check for start condition, allows user to break out of keyboard passthrough mode and use emulator controls */

uint8 cu_kbd_queue_in;
uint8 cu_kbd_queue_out;
uint8 cu_kbd_queue[512];

uint8 cu_kbd_held_lctrl;


/* Keyboard Dongle defines */
#define KBD_STOP	0
#define KBD_TX_START	1
#define KBD_TX_READY	2

#define KBD_SEND_KEY 0x00
#define KBD_SEND_END 0x01
#define KBD_SEND_DEVICE_ID 0x02
#define KBD_SEND_FIRMWARE_REV 0x03
#define KBD_RESET 0x7f

#define KBD_MODIFY_KEYBOARD	0x5E /* Ctrl+F1, disable/enable  keyboard */
#define KBD_MODIFY_MOUSE	0x5F /* Ctrl+F2, adjust mouse in emulator while in keyboard mode */
void cu_kbd_handle_key(SDL_Event const* ev);
auint cu_kbd_process(auint prev, auint curr);

//scan codes
const auint cu_kbd_scan_codes[][2] = {
	{0x0d,SDLK_TAB},
	{0x0e,SDLK_BACKQUOTE},// `
	{0x12,SDLK_LSHIFT},
	{0x15,SDLK_q},
	{0x16,SDLK_1},
	{0x1a,SDLK_z},
	{0x1b,SDLK_s},
	{0x1c,SDLK_a},
	{0x1d,SDLK_w},
	{0x1e,SDLK_2},
	{0x21,SDLK_c},
	{0x22,SDLK_x},
	{0x23,SDLK_d},
	{0x24,SDLK_e},
	{0x25,SDLK_4},
	{0x26,SDLK_3},
	{0x29,SDLK_SPACE},
	{0x2a,SDLK_v},
	{0x2b,SDLK_f},
	{0x2c,SDLK_t},
	{0x2d,SDLK_r},
	{0x2e,SDLK_5},
	{0x31,SDLK_n},
	{0x32,SDLK_b},
	{0x33,SDLK_h},
	{0x34,SDLK_g},
	{0x35,SDLK_y},
	{0x36,SDLK_6},
	{0x3a,SDLK_m},
	{0x3b,SDLK_j},
	{0x3c,SDLK_u},
	{0x3d,SDLK_7},
	{0x3e,SDLK_8},
	{0x41,SDLK_COMMA},
	{0x42,SDLK_k},
	{0x43,SDLK_i},
	{0x44,SDLK_o},
	{0x45,SDLK_0},
	{0x46,SDLK_9},
	{0x49,SDLK_PERIOD},
	{0x4a,SDLK_SLASH},
	{0x4b,SDLK_l},
	{0x4c,SDLK_SEMICOLON},
	{0x4d,SDLK_p},
	{0x4e,SDLK_MINUS},
	{0x52,SDLK_QUOTE},
	{0x54,SDLK_LEFTBRACKET},
	{0x55,SDLK_EQUALS},
	{0x59,SDLK_RSHIFT},
	{0x5a,SDLK_RETURN},
	{0x5b,SDLK_RIGHTBRACKET},
	{0x5d,SDLK_BACKSLASH},
	{0x66,SDLK_BACKSPACE},
	{0x69,SDLK_KP_1},
	{0x6b,SDLK_KP_4},
	{0x6c,SDLK_KP_7},
	{0x70,SDLK_KP_0},
	{0x71,SDLK_KP_PERIOD},
	{0x72,SDLK_KP_2},
	{0x73,SDLK_KP_5},
	{0x74,SDLK_KP_6},
	{0x75,SDLK_KP_8},
	{0x79,SDLK_KP_PLUS},
	{0x7a,SDLK_KP_3},
	{0x7b,SDLK_KP_MINUS},
	{0x7c,SDLK_KP_MULTIPLY},
	{0x7d,SDLK_KP_9},
	{0,0}
};


#endif /* KEYBOARD_H */
