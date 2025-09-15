/*
 *  UzeRom file parser
 *
 *  Copyright (C) 2016 - 2017
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



#ifndef CU_UFILE_H
#define CU_UFILE_H



#include "types.h"



#define PERIPHERAL_MOUSE 1
#define PERIPHERAL_KEYBOARD 2
#define PERIPHERAL_MULTITAP 4
#define PERIPHERAL_ESP8266 8

#define JAMMA_ROTATE_90 1
#define JAMMA_ROTATE_180 2
#define JAMMA_ROTATE_270 4
#define JAMMA_FLIP_H 8
#define JAMMA_FLIP_V 16
#define JAMMA_B0 32 //future use...
#define JAMMA_B1 64
#define JAMMA_B2 128



/*
** UzeRom header provided information
*/
typedef struct{
 auint version;     /* Header version (always 1) */
 auint target;      /* Target UC; ATMega644 = 0 */
 auint pmemsize;    /* Occupied program memory size */
 auint year;        /* Release year */
 uint8 name[32];    /* Name of program (Zero terminated) */
 uint8 author[32];  /* Name of author (Zero terminated) */
 uint8 icon[256];   /* 16 x 16 icon using the Uzebox palette */
 auint crc32;       /* CRC of the program */
 uint8 psupport;    /* Peripherals supported */
 uint8 desc[64];    /* Description (Zero terminated) */
 uint8 pdefault;    /* Peripherals default on(Emulator) */
 uint8 jamma;       /* JAMMA Options(Rotation/Mirroring) */
 uint8 spiram_banks;/* Ideal SPI RAM size(in 64K banks) */
 /* uint8 reserved[111] */
}cu_ufile_header_t;


/*
** Attempts to load the passed file into code memory. The code memory is not
** cleared, so bootloader image or other contents may be added before this if
** there are any.
**
** The code memory must be 64 KBytes.
**
** Returns TRUE if the loading was successful.
*/
boole cu_ufile_load(char const* fname, uint8* cmem, cu_ufile_header_t* head);


#endif
