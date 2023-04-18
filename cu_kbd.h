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

auint cu_kbd_state;
auint cu_kbd_clock;
auint cu_kbd_data_in;
auint cu_kbd_data_out;
auint cu_kbd_enabled;
auint cu_kbd_bypassed; /* don't check for start condition, allows user to break out of keyboard passthrough mode and use emulator controls */

auint cu_kbd_queue_in;
auint cu_kbd_queue_out;
uint8 cu_kbd_queue[512];

auint cu_kbd_held_lctrl;


/* Keyboard Dongle defines */
#define KBD_STOP      0
#define KBD_TX_START  1
#define KBD_TX_READY  2

#define KBD_SEND_KEY 0x00
#define KBD_SEND_END 0x01
#define KBD_SEND_DEVICE_ID 0x02
#define KBD_SEND_FIRMWARE_REV 0x03
#define KBD_RESET 0x7f

#define KBD_MODIFY_KEYBOARD  0x5E /* Ctrl+F1, disable/enable  keyboard */
#define KBD_MODIFY_MOUSE     0x5F /* Ctrl+F2, adjust mouse in emulator while in keyboard mode */
void cu_kbd_handle_key(SDL_Event const* ev);
auint cu_kbd_process(auint prev, auint curr);




/*
** Set the keyboard status to enabled
*/
void  cu_kbd_set_enabled(uint8 val);



/*
** Get the keyboard enabled status
*/
auint  cu_kbd_get_enabled(void);


#endif /* KEYBOARD_H */
