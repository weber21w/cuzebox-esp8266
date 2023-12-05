/*
 *  SNES Mouse Emulation
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


#include "cu_mouse.h"

uint8 cu_mouse_enabled;
sint32 cu_mouse_dx, cu_mouse_dy;
uint8 cu_mouse_buttons;
uint8 cu_mouse_scale;

void cu_adjust_mouse_scale(){ /* TODO replace the implementation in main.c, */
/* TODO have keyboard ctrl+alt+m toggle mouse sensitivity */
     cu_mouse_scale++;
     if(cu_mouse_scale == 1U){ /* mouse just disabled */
      cu_mouse_enabled = 1U;
      print_unf("SNES Mouse Enabled(overriding P1)\n");
     }else if(cu_mouse_scale == 6U){ /* wrapped around sensitivity, disable(user can re-enable and go back through scale) */
      cu_mouse_enabled = 0U;
      cu_mouse_scale = 0U;
      print_unf("SNES Mouse Disabled(P1 restored)\n");
     }else
      print_message("SNES Mouse Sensitivity: %d\n", cu_mouse_scale);
}


auint cu_mouse_encode_delta(int d){
 uint8 result;
 if (d < 0){
   result = 0;
   d = -d;
 }else
  result = 1;

 if (d > 127)
  d = 127;
 if (!(d & 64))
  result |= 2;
 if (!(d & 32))
  result |= 4;
 if (!(d & 16))
  result |= 8;
 if (!(d & 8))
  result |= 16;
 if (!(d & 4))
  result |= 32;
 if (!(d & 2))
  result |= 64;
 if (!(d & 1))
  result |= 128;
 return result & 0xFF;
}


void cu_mouse_update(){

// if (cu_mouse_enabled){
//  if ((ev->type) == SDL_MOUSEMOTION){ /* http://www.repairfaq.org/REPAIR/F_SNES.html we always report "low sensitivity" */

// cu_mouse_buttons = SDL_GetRelativeMouseState(&cu_mouse_dx,&cu_mouse_dy);
// cu_mouse_dx >>= cu_mouse_scale;
// cu_mouse_dy >>= cu_mouse_scale;

// if (guicore_getflags() & 0x0001U){ /* GUICORE_FULLSCREEN HACK TODO */
//  SDL_WarpMouseInWindow(guicore_window,400,300); /* keep mouse centered so it doesn't get stuck on the edge of the screen... */
//  SDL_GetRelativeMouseState(&cu_mouse_dx,&cu_mouse_dy); /*...and immediately consume the bogus motion event it generated. */
// }
//  }
// }

}



auint cu_mouse_process(auint prev, auint curr){
/*
 buttons[0] = (encode_delta(cu_mouse_dx) << 24) | (encode_delta(cu_mouse_dy) << 16) | 0x7FFF;
 if (cu_mouse_buttons & SDL_BUTTON_LMASK)
  buttons[0] &= ~(1<<9);
 if (cu_mouse_buttons & SDL_BUTTON_RMASK)
  buttons[0] &= ~(1<<8);
*/
 return curr;
}


auint  cu_mouse_get_enabled(void){
 return cu_mouse_enabled;
}


void cu_mouse_set_enabled(uint8 val){
 cu_mouse_enabled = val;
}


auint  cu_mouse_get_buttons(void){
 return cu_mouse_buttons;
}


void cu_mouse_set_buttons(uint8 val){
 cu_mouse_buttons = val;
}


auint  cu_mouse_get_scale(void){
 return cu_mouse_scale;
}


void cu_mouse_set_scale(uint8 val){
 cu_mouse_scale = val;
}


void  cu_mouse_get_position(sint32 *mx, sint32 *my){
 *mx = cu_mouse_dx;
 *my = cu_mouse_dy;
}


void cu_mouse_set_position(sint32 mx, sint32 my){
 cu_mouse_dx = mx;
 cu_mouse_dy = my;
}
