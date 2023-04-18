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
#include "cu_mouse.h"


/* Scan codes */
const auint cu_kbd_scan_codes[][2] = {
 {0x0d,SDLK_TAB},
 {0x0e,SDLK_BACKQUOTE},
/* {0x11,SDLK_LALT}, */
 {0x12,SDLK_LSHIFT},
/* {0x14,SDLK_LCTRL}, */
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
   cu_kbd_enabled = 1U; /* enable keyboard capture for Uzebox Keyboard Dongle */
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


auint  cu_kbd_get_enabled(void){
 return cu_kbd_enabled;
}


void cu_kbd_set_enabled(uint8 val){
 cu_kbd_enabled = val;
}
