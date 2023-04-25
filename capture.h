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



#ifndef CAPTURE_H
#define CAPTURE_H



#include "types.h"

#ifndef ENABLE_IREP
/*
** Captures the state of the button inputs for Player 1 and streams them
** to a file where they can be played back later.
*/
void capture_inputs(void);
#else
/*
** Replays the file containing button inputs for Player 1 for each frame,
** overriding any physical button inputs for the duration of the file.
** Returns TRUE when the replay first reaches the end of the file.
*/
boole capture_replay(void);
#endif

/*
** Closes the capture file, if opened
*/
void capture_finalize(void);


#endif
