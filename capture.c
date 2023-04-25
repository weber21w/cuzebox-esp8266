/*
 *  Input Capture & Replay (Compatible with Uzem capture files)
 *
 *  Copyright (C) 2022
      Matt Pandina (Artcfox)
 *  CUzeBox is Copyright (C) 2016
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



#include "capture.h"
#include "cu_ctr.h"

/* Identifies whether any input capturing/replaying was performed */
static boole capture_isinit = FALSE;

/* Indicates whether the input capture or replay is finished (or had an error) */
static boole capture_isdone = FALSE;

/* FILE pointer for input capture */
static FILE* capture_file = NULL;

/* Filename of the capture file */
static const char* capture_name = "input.cap";

#ifndef ENABLE_IREP
/*
** Captures the state of the button inputs for Player 1 and streams them
** to a file where they can be played back later.
*/
void capture_inputs(void)
{
 auint buttons = 0U;

 if (capture_isdone){ return; }

 /* Initialize input capturing if necessary */
 if (!capture_isinit){
  capture_file = fopen(capture_name, "wb"); /* If it exists, we want truncation */
  if (!capture_file){
   capture_isdone = TRUE;
   return;
  }
  capture_isinit = TRUE;
 }

 cu_ctr_getsnes(0, &buttons);
 buttons = ~buttons;
 fputc((uint8)(buttons & 0xFF), capture_file);
 fputc((uint8)((buttons >> 8) & 0xFF), capture_file);
}
#else
/*
** Replays the file containing button inputs for Player 1 for each frame,
** overriding any physical button inputs for the duration of the file.
** Returns TRUE when the replay first reaches the end of the file.
*/
boole capture_replay(void)
{
 size_t rb;
 uint8 buf[2];
 auint buttons;

 if (capture_isdone){ return FALSE; }

 if (!capture_isinit){
  capture_file = fopen(capture_name, "rb");
  if (!capture_file){
   capture_isdone = TRUE;
   return FALSE;
  }
  capture_isinit = TRUE;
 }

 rb = fread(buf, 1, 2, capture_file);
 if (rb == 2){
  buttons = ~((auint)(buf[0]) | ((auint)(buf[1]) << 8));
  cu_ctr_setsnes(0, buttons);
 }else{
  capture_isdone = TRUE;
 }
 return capture_isdone;
}
#endif


/*
** Closes the capture file, if opened
*/
void capture_finalize(void)
{
 if (capture_isinit){
  fclose(capture_file);
 }
}
