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

#include "cu_spir.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* SPI RAM state */
static cu_state_spir_t spir_state;

/* SPI RAM state machine */
/* Parameter position mask & shift */
#define STAT_PPMASK  0xC0U
#define STAT_PPSH    6U
/* Read (Data bytes) */
#define STAT_READB   0x00U
/* Wait for instruction */
#define STAT_IDLE    0x01U
/* Read */
#define STAT_READ    0x02U
/* Write */
#define STAT_WRITE   0x03U
/* Write (data bytes) */
#define STAT_WRITEB  0x04U
/* Read mode register */
#define STAT_RMODE   0x05U
/* Write mode register */
#define STAT_WMODE   0x06U
/* Sink (don't accept further data) */
#define STAT_SINK    0x07U

/* Helper for power-of-2 clamps (round up, 32-bit is sufficient here) */
static auint ceil_pow2(auint x)
{
 if (x <= 1U)
  return 1U;
 x--;
 x |= x >> 1;
 x |= x >> 2;
 x |= x >> 4;
 x |= x >> 8;
 x |= x >> 16;
 return x + 1U;
}

/* Keep masks in sync (safe if size==0) */
static void spir_recompute_masks(void)
{
 spir_state.addr_mask = (spir_state.size ? (spir_state.size - 1U) : 0U);   /* size must be power-of-two */
 spir_state.page_mask = spir_state.page_size - 1U;
}

/* Ensure heap capacity for 'need' bytes; zero-fill new tail */
static boole spir_ensure_capacity(auint need)
{
 if (spir_state.alloc_bytes >= need)
  return TRUE;

 uint8 *newp = (uint8*)realloc(spir_state.ram, need);
 if (!newp){
  print_error("SPIRAM: realloc to %u bytes failed\n", (unsigned)need);
  return FALSE;
 }

 /* zero the newly added tail for determinism */
 if (spir_state.alloc_bytes < need)
  memset(newp + spir_state.alloc_bytes, 0, need - spir_state.alloc_bytes);

 spir_state.ram = newp;
 spir_state.alloc_bytes = need;
 return TRUE;
}

/*
** Resets SPI RAM peripheral. Cycle is the CPU cycle when it happens which
** might be used for emulating timing constraints.
** Preserve heap buffer across reset and do not assume a size if unallocated.
*/
void  cu_spir_reset(auint cycle)
{
 uint8 *saved_ram   = spir_state.ram;
 auint  saved_alloc = spir_state.alloc_bytes;

 memset(&spir_state, 0, sizeof(spir_state));

 spir_state.ram         = saved_ram;
 spir_state.alloc_bytes = saved_alloc;

 spir_state.mode      = 0x40U;    /* Sequential mode */
 spir_state.data      = 0xFFU;
 spir_state.state     = STAT_IDLE;
 spir_state.page_size = 32;       /* Default for all chip types */

 /* visible size = what we actually have (may be 0 before set_size) */
 spir_state.size = saved_alloc;

 spir_recompute_masks();

 /* addressing based on current visible size (2 bytes for 64K, else 3) */
 spir_state.addr_bytes = (spir_state.size && (spir_state.size <= 65536U)) ? 2U : 3U;
}

/*
** Sets chip select's state, TRUE to enable, FALSE to disable.
*/
void  cu_spir_cs_set(boole ena, auint cycle)
{
 if ( (spir_state.ena && (!ena)) ||
      ((!spir_state.ena) && ena) ){
  spir_state.ena = ena;
  if (spir_state.ena == FALSE){ /* Back to idle state */
   spir_state.state = STAT_IDLE;
  }
 }
}

/*
** Sends a byte of data to the SPI RAM. The passed cycle corresponds the cycle
** when it was clocked out of the AVR.
*/
void  cu_spir_send(auint data, auint cycle)
{
 auint ppos;

 /* This fast path serves reads, beneficial if the SPI RAM is used for
 ** generating video data.
 */

 if (spir_state.state == STAT_READB){
  if (spir_state.mode == 0x80U){ /* Page mode */
   spir_state.addr = (spir_state.addr & ~((auint)spir_state.page_mask)) +
                     ((spir_state.addr + 1U) &  spir_state.page_mask);
  }else if (spir_state.mode == 0x40U){ /* Sequential mode */
   spir_state.addr ++;
  }else{
   /* no-op */
  }
  spir_state.data  = spir_state.ram[spir_state.addr & spir_state.addr_mask];
  return;
 }

 /* Normal SPI RAM processing (excluding processing reads) */

 spir_state.data = 0xFFU; /* Default data out */

 if (spir_state.ena){     /* Good, this only tampers with the bus if actually enabled (unlike the SD card...) */

  switch (spir_state.state & (~STAT_PPMASK)){

   case STAT_IDLE:        /* Wait for valid command byte */

    switch (data){
     case 0x01U:          /* Write mode register */
      spir_state.state = STAT_WMODE;
      break;
     case 0x02U:          /* Write data */
      spir_state.state = STAT_WRITE;
      spir_state.addr  = 0U;
      break;
     case 0x03U:          /* Read data */
      spir_state.state = STAT_READ;
      spir_state.addr  = 0U;
      break;
     case 0x05U:          /* Read mode register */
      spir_state.state = STAT_RMODE;
      spir_state.data  = spir_state.mode;
      break;
     default:             /* Ignore all else, going into sinking data */
      spir_state.state = STAT_SINK;
      break;
    }
    break;

   case STAT_READ:        /* Read (preparation) */

    ppos = (spir_state.state & STAT_PPMASK) >> STAT_PPSH;
    spir_state.addr |= (auint)data << (((auint)(spir_state.addr_bytes - 1U) - ppos) * 8U);
    ppos ++;
    if (ppos >= spir_state.addr_bytes){
     spir_state.state = STAT_READB;
     spir_state.data  = spir_state.ram[spir_state.addr & spir_state.addr_mask]; /* First data byte */
    }else{
     spir_state.state = STAT_READ | (ppos << STAT_PPSH);
    }
    break;

   case STAT_WRITE:       /* Write (preparation) */

    ppos = (spir_state.state & STAT_PPMASK) >> STAT_PPSH;
    spir_state.addr |= (auint)data << (((auint)(spir_state.addr_bytes - 1U) - ppos) * 8U);
    ppos ++;
    if (ppos >= spir_state.addr_bytes){
     spir_state.state = STAT_WRITEB;
    }else{
     spir_state.state = STAT_WRITE | (ppos << STAT_PPSH);
    }
    break;

   case STAT_WRITEB:      /* Write (data bytes) */

    spir_state.ram[spir_state.addr & spir_state.addr_mask] = (uint8)data;
    if       (spir_state.mode == 0x80U){ /* Page mode */
     spir_state.addr = (spir_state.addr & ~((auint)spir_state.page_mask)) +
                       ((spir_state.addr + 1U) &  spir_state.page_mask);
    }else if (spir_state.mode == 0x40U){ /* Sequential mode */
     spir_state.addr = (spir_state.addr + 1U) & spir_state.addr_mask;
    }else{
     /* no-op */
    }
    break;

   case STAT_RMODE:       /* Read mode register */

    spir_state.state = STAT_SINK;
    break;

   case STAT_WMODE:       /* Write mode register */

    spir_state.mode = data & 0xC0U;
    spir_state.state = STAT_SINK;
    break;

   default:               /* Sinking data until CS is deasserted */

    break;

  }

 }
}



/*
** Receives a byte of data from the SPI RAM. The passed cycle corresponds the
** cycle when it must start to clock into the AVR. 0xFF is sent when the card
** tri-states the line.
*/
auint cu_spir_recv(auint cycle)
{
 return spir_state.data;
}



/*
** Returns SPI RAM state. It may be written, then the cu_spir_update()
** function has to be called to rebuild any internal state depending on it.
*/
cu_state_spir_t* cu_spir_get_state(void)
{
 return &spir_state;
}



/*
** Rebuild internal state according to the current state. Call after writing
** the SPI RAM state.
** Robust if size==0 (e.g., before set_size).
*/
void  cu_spir_update(void)
{
 spir_recompute_masks();
 if (spir_state.size && (spir_state.size <= 65536U)){
  spir_state.addr_bytes = 2U;
 }else{
  spir_state.addr_bytes = 3U;
 }
}



/*
** Set the number of 64K banks (if 1, we assume 16-bit addressing (23LC512), otherwise 24-bit).
** If a non-power-of-two is requested, round up to the next power of two, then clamp to MAX.
** Allocates or grows the heap as needed.
*/
void  cu_spir_set_size(auint banks)
{
 if (!banks)
  banks = 1U;

 /* round up banks to next power of two */
 auint rounded = ceil_pow2(banks);
 if (rounded != banks){
  print_error("SPIRAM: requested banks %u not power-of-two — rounded up to %u\n",
              (unsigned)banks, (unsigned)rounded);
  banks = rounded;
 }

 auint req = banks * 65536U;

 if (req > MAX_SPI_RAM_SIZE){
  print_error("SPIRAM: requested %u banks (%u bytes) exceeds max — clamped to %u bytes\n",
              (unsigned)banks, (unsigned)req, (unsigned)MAX_SPI_RAM_SIZE);
  banks = MAX_SPI_RAM_SIZE / 65536U;
  req   = MAX_SPI_RAM_SIZE;
 }

 if (!spir_ensure_capacity(req))
  return;

 spir_state.size = req;
 spir_recompute_masks();
 spir_state.addr_bytes = (banks > 1U) ? 3U : 2U;
}



/*
** Dump SPI RAM state to file for debugging.
*/
void  cu_spir_dump(void)
{
 FILE * f = fopen("spir_dump.bin", "wb");
 if (f != NULL){
  fwrite(spir_state.ram, 1, spir_state.size, f);
  fclose(f);
 }
}



/*
** Optional: free heap buffer on program shutdown.
*/
void  cu_spir_deinit(void)
{
 if (spir_state.ram){
  free(spir_state.ram);
  spir_state.ram = NULL;
 }
 spir_state.alloc_bytes = 0U;
 spir_state.size = 0U;
 spir_state.addr = 0U;
 spir_state.data = 0U;
 spir_state.state = STAT_IDLE;
}