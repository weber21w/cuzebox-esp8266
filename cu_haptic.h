/*
 *  Haptic Feedback Emulation(AKA Rumble)
 *
 *  Copyright (C) 2016 - 2025
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


#ifndef CU_HAPTIC_H
#define CU_HAPTIC_H



#include "types.h"



/* UzeBus opcode: payload = mask(1) + duration_ms(2), little-endian */
#define UHB_OP_RUMBLE 0x10U



/* Motor bit mask */
#define CU_HAP_MOTOR_LO 0x01U
#define CU_HAP_MOTOR_HI 0x02U



/* Logical ports (P1, P2, Multitap slots 1..3) */
typedef enum{
 CU_HAP_P1 = 0,
 CU_HAP_P2 = 1,
 CU_HAP_M0 = 2,
 CU_HAP_M1 = 3,
 CU_HAP_M2 = 4,
 CU_HAP_MAX = 5
} cu_hap_port_t;



/* Minimal public state (for emulator state dumps) */
typedef struct{
 auint last_mask;   /* Last mask applied (bit0=LO, bit1=HI) */
 auint last_dur_ms; /* Last duration commanded (ms) */
} cu_state_hap_t;



/*
** Initializes haptic subsystem. If 'ena' is FALSE, functions become no-ops.
*/
void  cu_hap_init(boole ena);



/*
** Shuts down haptic subsystem and releases host handles if any.
*/
void  cu_hap_quit(void);



/*
** Binds a CU port to a preferred host device index (SDL joystick index).
** If not called, the first available device is used lazily on first use.
** (Has effect only under SDL2 builds.)
*/
void  cu_hap_bind(cu_hap_port_t port, auint host_index);



/*
** Starts rumble on 'port' with 'motor_mask' (bits CU_HAP_MOTOR_*).
** Duration is in milliseconds; 0 is clamped to 1. New calls replace previous.
*/
void  cu_hap_start(cu_hap_port_t port, auint motor_mask, auint duration_ms);



/*
** Stops rumble on 'port' immediately (best effort on host).
*/
void  cu_hap_stop(cu_hap_port_t port);



/*
** Consumes a UzeBus frame targeting 'port'. Expects at least 4 bytes:
** [0]=UHB_OP_RUMBLE, [1]=mask, [2..3]=duration_ms (LE).
*/
void  cu_hap_from_uzebus(cu_hap_port_t port, const uint8* data, auint len);



/*
** Returns pointer to public state block (for state dumps).
*/
cu_state_hap_t* cu_hap_get_state(cu_hap_port_t port);



/*
** Rebuild internal state according to current state (after editing dump).
*/
void  cu_hap_update(void);



#endif