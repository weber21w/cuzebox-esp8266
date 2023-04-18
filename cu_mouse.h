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

#ifndef MOUSE_H
#define MOUSE_H

#include "types.h"

uint8 cu_mouse_enabled;
sint32 cu_mouse_dx, cu_mouse_dy;
uint8 cu_mouse_buttons;
uint8 cu_mouse_scale;

void cu_adjust_mouse_scale();


void cu_mouse_update();


auint cu_mouse_process(auint prev, auint curr);


/*
** Get the mouse enabled status
*/
auint  cu_mouse_get_enabled(void);


/*
** Set the mouse status to enabled
*/
void  cu_mouse_set_enabled(uint8 val);


/*
** Get the mouse position
*/
void  cu_mouse_get_position(sint32 *mx, sint32 *my);


/*
** Set the mouse position
*/
void  cu_mouse_set_position(sint32 mx, sint32 my);


/*
** Get the mouse buttons
*/
auint  cu_mouse_get_buttons(void);


/*
** Set the mouse buttons
*/
void  cu_mouse_set_buttons(uint8 val);



/*
** Get the mouse scale
*/
auint  cu_mouse_get_scale(void);
/*
** Set the mouse scale
*/
void  cu_mouse_set_scale(uint8 val);

#endif /* MOUSE_H */
