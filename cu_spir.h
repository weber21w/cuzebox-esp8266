/*
 *  SPI RAM peripheral (on SPI bus)
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

#ifndef CU_SPIR_H
#define CU_SPIR_H

#include "types.h"

#ifndef MAX_SPI_RAM_SIZE
 #define MAX_SPI_RAM_SIZE (128UL*64UL*1024UL) /* hypothetical chip up to 8MB */
#endif

#if (MAX_SPI_RAM_SIZE > 0x01000000U)
#error "SPI RAM > 16MiB requires 4-byte addressing; widen addr_bytes/PP field"
#endif



/* SPI RAM state structure. This isn't really meant to be edited, but it is
** necessary for emulator state dumps. Every value is at most 32 bits. */
typedef struct{
 uint8 *ram;        /* heap buffer (size bytes valid) */
 auint alloc_bytes;/* current heap capacity (>= size) */
 boole ena;        /* Chip select state, TRUE: enabled (CS low) */
 auint mode;       /* Mode register's contents (on bit 6 and 7) */
 auint state;      /* SPI RAM state machine */
 auint addr;       /* Address within the RAM */
 auint data;       /* Data waiting to get on the output (8 bits) */
 auint size;       /* visible size in bytes (power-of-two) */
 uint8 addr_bytes; /* 3 for 128K/512K parts, 2 for 64K parts */
 auint addr_mask;  /* mask for address range calculations (size-1) */
 auint page_size;  /* default 32, configurable to 256 on 23AA0XM */
 auint page_mask;  /* page calculations (32 for 23LC1024, configurable 256 for 23AA0XM) */
}cu_state_spir_t;



/*
** Resets SPI RAM peripheral. Cycle is the CPU cycle when it happens which
** might be used for emulating timing constraints.
*/
void  cu_spir_reset(auint cycle);



/*
** Sets chip select's state, TRUE to enable, FALSE to disable.
*/
void  cu_spir_cs_set(boole ena, auint cycle);



/*
** Sends a byte of data to the SPI RAM. The passed cycle corresponds the cycle
** when it was clocked out of the AVR.
*/
void  cu_spir_send(auint data, auint cycle);



/*
** Receives a byte of data from the SPI RAM. The passed cycle corresponds the
** cycle when it must start to clock into the AVR. 0xFF is sent when the card
** tri-states the line.
*/
auint cu_spir_recv(auint cycle);



/*
** Returns SPI RAM state. It may be written, then the cu_spir_update()
** function has to be called to rebuild any internal state depending on it.
*/
cu_state_spir_t* cu_spir_get_state(void);



/*
** Rebuild internal state according to the current state. Call after writing
** the SPI RAM state.
*/
void  cu_spir_update(void);



/*
** Set the number of 64K banks (if 1, we assume 2 byte addressing, otherwise 3).
** Non power-of-two requests are rounded up to the next power of two, then clamped to MAX.
*/
void  cu_spir_set_size(auint banks);



/*
** Dump SPI RAM state to file for debugging.
*/
void  cu_spir_dump(void);



/*
** Optional: free heap buffer on program shutdown.
*/
void  cu_spir_deinit(void);

#endif
