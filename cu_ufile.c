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



#include "cu_ufile.h"
#include "filesys.h"



/* UzeRom header magic value */
#define CU_MAGIC_LEN 6U
static const uint8 cu_magic[] = {'U', 'Z', 'E', 'B', 'O', 'X'};


/* String constants */
static const char cu_id[]  = "UzeRom parser: ";
static const char cu_err[] = "Error: ";
static const char cu_war[] = "Warning: ";

#define PERIPHERAL_MOUSE 1
#define PERIPHERAL_KEYBOARD 2
#define PERIPHERAL_MULTITAP 4
#define PERIPHERAL_ESP8266 8

/*
** Attempts to load the passed file into code memory. The code memory is not
** cleared, so bootloader image or other contents may be added before this if
** there are any.
**
** The code memory must be 64 KBytes.
**
** Returns TRUE if the loading was successful.
*/
boole cu_ufile_load(char const* fname, uint8* cmem, cu_ufile_header_t* head)
{
 asint rv;
 uint8 buf[512];
 auint i;
 auint len;
 sint8 psupport_str[256];
 sint8 pdefault_str[256];

 if (!filesys_open(FILESYS_CH_EMU, fname)){
  print_error("%s%sCouldn't open %s.\n", cu_id, cu_err, fname);
  goto ex_none;
 }

 rv = filesys_read(FILESYS_CH_EMU, buf, 512U);
 if (rv != 512){
  print_error("%s%sNot enough data for header in %s.\n", cu_id, cu_err, fname);
  goto ex_file;
 }

 for (i = 0U; i < CU_MAGIC_LEN; i++){
  if (buf[i] != cu_magic[i]){
   print_error("%s%s%s is not a UzeRom file.\n", cu_id, cu_err, fname);
   goto ex_file;
  }
 }

 if (buf[6] != 1U){
  print_error("%s%s%s has unknown version (%d).\n", cu_id, cu_war, fname, buf[6]);
 }
 head->version = buf[6];

 if (buf[7] != 0U){
  print_error("%s%s%s has unknown target UC (%d).\n", cu_id, cu_err, fname, buf[7]);
  goto ex_file;
 }
 head->target = buf[7];

 len = ((auint)(buf[ 8])      ) +
       ((auint)(buf[ 9]) <<  8) +
       ((auint)(buf[10]) << 16) +
       ((auint)(buf[11]) << 24);
 if (len > 65535U){
  print_error("%s%s%s has too large program size (%d).\n", cu_id, cu_err, fname, len);
  goto ex_file;
 }
 head->pmemsize = len;

 head->year = ((auint)(buf[12])      ) +
              ((auint)(buf[13]) <<  8);

 for (i = 0U; i < 31U; i++){ head->name[i] = buf[14U + i]; }
 head->name[31] = 0U;

 for (i = 0U; i < 31U; i++){ head->author[i] = buf[46U + i]; }
 head->author[31] = 0U;

 auint icon_crc = 0;
 for (i = 0U; i < 256U; i++){
  head->icon[i] = buf[78U + i];
  icon_crc += head->icon[i];
 }
/*
 if (icon_crc){
  uint8 x,y;
  for (y = 0U; y < 8U; y++){
   for (x = 0U; x < 8U; x++){
  
   }
   print_unf(" ");
  }
 }
*/
 head->crc32 = ((auint)(buf[334])      ) +
               ((auint)(buf[335]) <<  8) +
               ((auint)(buf[336]) << 16) +
               ((auint)(buf[337]) << 24);

 head->psupport = buf[338]; /* the peripherals the ROM supports */
 if (head->psupport & PERIPHERAL_MOUSE){ sprintf((char *)psupport_str+strlen((char *)psupport_str), "Mouse,");}
 if (head->psupport & PERIPHERAL_KEYBOARD){ sprintf((char *)psupport_str+strlen((char *)psupport_str), "Keyboard,"); }
 if (head->psupport & PERIPHERAL_MULTITAP){ sprintf((char *)psupport_str+strlen((char *)psupport_str), "Multitap,"); }
 if (head->psupport & PERIPHERAL_ESP8266){ sprintf((char *)psupport_str+strlen((char *)psupport_str), "ESP8266,"); }
 if (strlen((char *)psupport_str) && psupport_str[strlen((char *)psupport_str)-1] == ','){ psupport_str[strlen((char *)psupport_str)-1] = '\0'; } /* remove trailing comma, if present */

 for (i = 0U; i < 63U; i++){ head->desc[i] = buf[339U + i]; }
 head->desc[63] = 0U;

 head->pdefault = buf[339U+64U+1U]; /* the peripherals that should be "connected" at start(save the user some hotkey presses) */
 if (head->pdefault & PERIPHERAL_MOUSE){ sprintf((char *)pdefault_str+strlen((char *)pdefault_str), "Mouse,");}
 if (head->pdefault & PERIPHERAL_KEYBOARD){ sprintf((char *)pdefault_str+strlen((char *)pdefault_str), "Keyboard,"); }
 if (head->pdefault & PERIPHERAL_MULTITAP){ sprintf((char *)pdefault_str+strlen((char *)pdefault_str), "Multitap,"); }
 if (head->pdefault & PERIPHERAL_ESP8266){ sprintf((char *)pdefault_str+strlen((char *)pdefault_str), "ESP8266,"); }
 if (strlen((char *)pdefault_str) && pdefault_str[strlen((char *)pdefault_str)-1] == ','){ pdefault_str[strlen((char *)pdefault_str)-1] = '\0'; } /* remove trailing comma, if present */

 rv = filesys_read(FILESYS_CH_EMU, cmem, len);
 if (rv != len){
  print_error("%s%sNot enough data for program in %s.\n", cu_id, cu_err, fname);
  goto ex_file;
 }

 filesys_flush(FILESYS_CH_EMU);

 /* Print out successful result */

 print_message(
  "%sSuccesfully loaded program from %s:\n", cu_id, fname);
 print_message(
  "\tName .......: %s\n"
  "\tAuthor .....: %s\n"
  "\tYear .......: %d\n"
  "\tDescription : %s\n"
  "\tSupported ..: %s\n",
  (char*)(&(head->name[0])),
  (char*)(&(head->author[0])),
  head->year,
  (char*)(&(head->desc[0])),
  (char*)(&(psupport_str)));

 /* Successful */

 return TRUE;

 /* Failing exits */

ex_file:
 filesys_flush(FILESYS_CH_EMU);

ex_none:
 return FALSE;
}
