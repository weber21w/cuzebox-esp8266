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



#include "cu_haptic.h"

/* Detect SDL major version if headers are present */
#if defined(SDL_MAJOR_VERSION) && (SDL_MAJOR_VERSION >= 2)
# define CU_HAP_HAVE_SDL2 1
#else
# define CU_HAP_HAVE_SDL2 0
#endif



typedef struct{
 boole             ena;
 auint             bound_index; /* Preferred SDL device index, -1U = auto */
#if CU_HAP_HAVE_SDL2
 SDL_GameController* gc;
 SDL_Joystick*       js;
 SDL_Haptic*         hp;
 boole               has_gc_rumble;
 boole               has_js_rumble;
 auint               opened_index;
#endif
 cu_state_hap_t pub; /* Public state for dumps */
} cu_hap_slot_t;

static struct{
 boole ena;
 cu_hap_slot_t slot[CU_HAP_MAX];
} cu_hap_ctx;



static void slot_reset(cu_hap_slot_t* s){
 memset(s, 0, sizeof(*s));
 s->bound_index = (auint)(-1);
#if CU_HAP_HAVE_SDL2
 s->gc = 0; s->js = 0; s->hp = 0;
 s->has_gc_rumble = 0; s->has_js_rumble = 0;
 s->opened_index = (auint)(-1);
#endif
 s->pub.last_mask = 0U;
 s->pub.last_dur_ms = 0U;
}

#if CU_HAP_HAVE_SDL2
static void slot_open_device(cu_hap_slot_t* s){
 if (s->gc || s->js) return;

 int target = (int)s->bound_index;
 if ((target < 0) || (target >= SDL_NumJoysticks())){
  target = -1;
  for (int i = 0, n = SDL_NumJoysticks(); i < n; i++){
   if (SDL_IsGameController(i)){ target = i; break; }
   if (target < 0) target = i;
  }
 }
 if (target < 0) return;

 if (SDL_IsGameController(target)){
  s->gc = SDL_GameControllerOpen(target);
  if (s->gc){
#  if SDL_VERSION_ATLEAST(2,0,9)
   s->has_gc_rumble = SDL_GameControllerHasRumble(s->gc) ? 1 : 0;
#  else
   s->has_gc_rumble = 1;
#  endif
   s->js = SDL_GameControllerGetJoystick(s->gc);
   s->opened_index = (auint)target;
  }
 }
 if (!s->gc){
  s->js = SDL_JoystickOpen(target);
  if (s->js){
#  if SDL_VERSION_ATLEAST(2,0,9)
   s->has_js_rumble = SDL_JoystickHasRumble(s->js) ? 1 : 0;
#  else
   s->has_js_rumble = 1;
#  endif
   s->opened_index = (auint)target;
  }
 }

 /* Optional fallback haptic */
 if (s->js && SDL_JoystickIsHaptic(s->js)){
  SDL_Haptic* hp = SDL_HapticOpenFromJoystick(s->js);
  if (hp && (SDL_HapticRumbleSupported(hp) == 1)){
   if (SDL_HapticRumbleInit(hp) == 0) s->hp = hp;
   else SDL_HapticClose(hp);
  }
 }
 if (!s->hp && (SDL_NumHaptics() > 0)){
  SDL_Haptic* hp = SDL_HapticOpen(0);
  if (hp && (SDL_HapticRumbleSupported(hp) == 1)){
   if (SDL_HapticRumbleInit(hp) == 0) s->hp = hp;
   else SDL_HapticClose(hp);
  }
 }
}
#endif



static void slot_start(cu_hap_slot_t* s, auint mask, auint duration_ms){
 if (!cu_hap_ctx.ena) return;
 if (duration_ms == 0U) duration_ms = 1U;

 /* Update public state */
 s->pub.last_mask   = mask & (CU_HAP_MOTOR_LO | CU_HAP_MOTOR_HI);
 s->pub.last_dur_ms = duration_ms;

#if CU_HAP_HAVE_SDL2
 slot_open_device(s);

 Uint16 lo = (s->pub.last_mask & CU_HAP_MOTOR_LO) ? 0xFFFFU : 0x0000U;
 Uint16 hi = (s->pub.last_mask & CU_HAP_MOTOR_HI) ? 0xFFFFU : 0x0000U;
 Uint32 ms = (Uint32)duration_ms;

# if SDL_VERSION_ATLEAST(2,0,14)
 if (s->gc && s->has_gc_rumble){
  SDL_GameControllerRumble(s->gc, lo, hi, ms);
  return;
 }
# endif
# if SDL_VERSION_ATLEAST(2,0,9)
 if (s->js && s->has_js_rumble){
  SDL_JoystickRumble(s->js, lo, hi, ms);
  return;
 }
# endif
 if (s->hp){
  float st = (lo || hi) ? 1.0f : 0.0f;
  if (st > 0.0f) SDL_HapticRumblePlay(s->hp, st, ms);
  else SDL_HapticRumbleStop(s->hp);
  return;
 }
#else
 (void)mask; (void)duration_ms;
#endif
}



static void slot_stop(cu_hap_slot_t* s){
 if (!cu_hap_ctx.ena) return;

 /* Update public state */
 s->pub.last_mask   = 0U;
 s->pub.last_dur_ms = 0U;

#if CU_HAP_HAVE_SDL2
 if (s->gc){
# if SDL_VERSION_ATLEAST(2,0,14)
  SDL_GameControllerRumble(s->gc, 0, 0, 0);
# endif
 }else if (s->js){
# if SDL_VERSION_ATLEAST(2,0,9)
  SDL_JoystickRumble(s->js, 0, 0, 0);
# endif
 }
 if (s->hp) SDL_HapticRumbleStop(s->hp);
#endif
}



void cu_hap_init(boole ena){
 cu_hap_ctx.ena = ena ? 1 : 0;
 for (auint i = 0; i < (auint)CU_HAP_MAX; i++){
  slot_reset(&cu_hap_ctx.slot[i]);
 }
#if CU_HAP_HAVE_SDL2
 if (cu_hap_ctx.ena){
  SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC);
 }
#endif
}



void cu_hap_quit(void){
#if CU_HAP_HAVE_SDL2
 for (auint i = 0; i < (auint)CU_HAP_MAX; i++){
  cu_hap_slot_t* s = &cu_hap_ctx.slot[i];
  if (s->hp){ SDL_HapticClose(s->hp); s->hp = 0; }
  if (s->gc){ SDL_GameControllerClose(s->gc); s->gc = 0; s->js = 0; }
  else if (s->js){ SDL_JoystickClose(s->js); s->js = 0; }
 }
#endif
 memset(&cu_hap_ctx, 0, sizeof(cu_hap_ctx));
}



void cu_hap_bind(cu_hap_port_t port, auint host_index){
 if ((auint)port >= (auint)CU_HAP_MAX) return;
 cu_hap_ctx.slot[port].bound_index = host_index;
}



void cu_hap_start(cu_hap_port_t port, auint motor_mask, auint duration_ms){
 if ((auint)port >= (auint)CU_HAP_MAX) return;
 slot_start(&cu_hap_ctx.slot[port], motor_mask, duration_ms);
}



void cu_hap_stop(cu_hap_port_t port){
 if ((auint)port >= (auint)CU_HAP_MAX) return;
 slot_stop(&cu_hap_ctx.slot[port]);
}



void cu_hap_from_uzebus(cu_hap_port_t port, const uint8* data, auint len){
 if ((auint)port >= (auint)CU_HAP_MAX) return;
 if (!data || (len < 4U)) return;
 if (data[0] != UHB_OP_RUMBLE) return;

 auint mask = (auint)data[1];
 auint dur  = (auint)( (auint)data[2] | ((auint)data[3] << 8) );

 slot_start(&cu_hap_ctx.slot[port], mask, dur);
}



cu_state_hap_t* cu_hap_get_state(cu_hap_port_t port){
 if ((auint)port >= (auint)CU_HAP_MAX) return 0;
 return &cu_hap_ctx.slot[port].pub;
}



void cu_hap_update(void){
 /* Nothing to rebuild. */
}