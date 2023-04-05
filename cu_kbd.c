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


#include "cu_kbd.h"
extern void cu_adjust_mouse_scale();

void cu_kbd_handle_key(SDL_Event const* ev){

	if(cu_kbd_queue_in == cu_kbd_queue_out)
		cu_kbd_queue_in = cu_kbd_queue_out = 0U;

	if(ev->type == SDL_KEYUP){
		if(cu_kbd_queue_in < sizeof(cu_kbd_queue)-2U){
			cu_kbd_queue[cu_kbd_queue_in++] = 0xF0;
		}
	}

	if(cu_kbd_queue_in > sizeof(cu_kbd_queue)-1U)
		return;

	uint16 i = 0;
	while(cu_kbd_scan_codes[i][1] && cu_kbd_scan_codes[i][1] != ev->key.keysym.sym)
		i++;

	if(cu_kbd_scan_codes[i][1] == ev->key.keysym.sym){/* a printable character? */
		cu_kbd_queue[cu_kbd_queue_in++] = cu_kbd_scan_codes[i][0];

	}else{/* check special codes for emulator only features */
		if(ev->type == SDL_KEYDOWN){
			if(ev->key.keysym.sym == SDLK_LCTRL)
				cu_kbd_held_lctrl = 1U;
			else if(cu_kbd_held_lctrl){
				if(ev->key.keysym.sym == SDLK_F1){ /* toggle keyboard, so emulator controls and gamepads can be used again */
					cu_kbd_enabled = !cu_kbd_enabled;
					cu_kbd_bypassed = 255U;
					print_unf("Keyboard Dongle Disconnected\n");
				}else if(ev->key.keysym.sym == SDLK_F6)
					cu_adjust_mouse_scale(); /* change mouse scale(which can also enable/disable the mouse while in keyboard passthrough mode) */
			}
		}else if(ev->type == SDL_KEYUP){
			if(ev->key.keysym.sym == SDLK_LCTRL)
				cu_kbd_held_lctrl = 0U;
		}
	}
}



auint cu_kbd_process(auint prev, auint curr){ /* TODO maybe just put the start condition check in gamepad section so we don't call this function unless needed... */

	if(cu_kbd_bypassed){ /* don't allow the program to detect the keyboard and start it(cu_kbd_enabled). This allows the user to break out of the keyboard passthrough, and control the emulator */
		/* this can be removed by pressing F1(cu_kbd_bypassed = 0 in main.c), which will allow detection to then re-enabled cu_kbd_enabled */
	}else if(cu_kbd_state == KBD_STOP){
		if((curr&0x0C)==0x04){ /* check dongle start condition: clock=low & latch=high simultaneously */
			if(!cu_kbd_enabled)
				print_unf("ROM tested for dongle: keyboard emulation enabled(P2 disabled)\n");

			cu_kbd_state = KBD_TX_START;
			cu_kbd_enabled = 1U;	/* enable keyboard capture for Uzebox Keyboard Dongle */
		}

	}else if(cu_kbd_state == KBD_TX_START){

		if((curr&0x0C) == 0x08){ /* check start condition pulse completed: clock=high & latch=low (normal state) */
			cu_kbd_state = KBD_TX_READY;
			cu_kbd_clock = 8U;
		}

	}else if(cu_kbd_state == KBD_TX_READY){

		if((prev & (1<<3)) && !(curr & (1<<3))){ /* clock went low */
			if(cu_kbd_clock == 8U){
				cu_kbd_data_out = 0U;
				/* returns only keys (no commands response yet) */
				if(cu_kbd_queue_in == cu_kbd_queue_out){ /* no data buffered? */
					cu_kbd_data_in = 0U;
				}else{
					if(cu_kbd_queue_in < sizeof(cu_kbd_queue)-1U){
						cu_kbd_data_in = cu_kbd_queue[cu_kbd_queue_out++];//uzeKbScanCodeQueue.front();
						if(cu_kbd_queue_in == cu_kbd_queue_out)//uzeKbScanCodeQueue.pop();
							cu_kbd_queue_in = cu_kbd_queue_out = 0U;
					}else /* queue full? */
						cu_kbd_queue_in = cu_kbd_queue_out = 0U;
				}
			}

			cu_kbd_data_out <<= 1; /* shift data out to keyboard, latch pin is used as "Data Out" */
			if(curr & 0x04) /* latch pin=1? */
				cu_kbd_data_out |= 1U;


			
			if(cu_kbd_data_in & 0x80) /* shift data in from keyboard */
				curr |= (0x02); /* set P2 data bit */
			else
				curr &= ~(0x02); /* clear P2 data bit */
				
			cu_kbd_data_in <<= 1U;

			cu_kbd_clock--;
			if(cu_kbd_clock == 0U){
				if(cu_kbd_data_out == KBD_SEND_END)
					cu_kbd_state = KBD_STOP;
				else
					cu_kbd_clock = 8U;
			}
		}
	}
	return curr; /* only possibly if modified if KBD_TX_READY */
}
