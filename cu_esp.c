/*
 *  ESP8266 peripheral (on UART)
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



#include "cu_esp.h"
#include "cu_types.h"

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
#include <windows.h>
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#pragma comment (lib, "Ws2_32.lib")
#else /* POSIX system(Linux, OSX, etc) */
#include <stdio.h>
#include <sys/socket.h> //For Sockets
#include <stdlib.h>
#include <netinet/in.h> //For the AF_INET (Address Family)

//	#include <sys/socket.h>
	#include <sys/ioctl.h>
//	#include <sys/types.h>
//	#include <netinet/in.h>
	#include <netinet/tcp.h>
	#include <arpa/inet.h>
	#include <netdb.h>
	#include <unistd.h>
	#include <errno.h>
#include <termios.h>
#include <fcntl.h>
#endif

#ifdef __EMSCRIPTEN__

//builds worked for EMSCRIPTEN before adding extra linker for threading...
//at least using "./websocket_to_posix_proxy 8080" seems to actually do what's needed...
#include <emscripten.h>
#include <emscripten/websocket.h>
#include <emscripten/threading.h>
#include <emscripten/posix_socket.h>

static EMSCRIPTEN_WEBSOCKET_T bridgeSocket = 0;
#endif

/* esp state */
static cu_state_esp_t esp_state;


auint cu_esp_uzebox_write_ready(auint cycle){ /* host side checking if it can write */

	if(cycle >= esp_state.write_ready_cycle){
		esp_state.write_ready_cycle = 0UL;
		if(esp_state.uart_tx_enabled)
			return (1<<UDRE0);
	}

	return 0;
}


void cu_esp_uzebox_write(uint8 val, auint cycle){ /* host side has written a UART byte */

//	print_message("ESP uzebox write %d(%c)\n", val, val);

	/* need to set a delay until a write can happen again */
	esp_state.write_ready_cycle = WRAP32(cycle + esp_state.uart_baud_bits); /* uart_baud_bits is recalculated everytime UART flags are changed */
	esp_state.write_buf[esp_state.write_buf_pos_in++] = val;

	if(!esp_state.uart_tx_enabled)
		return;

	//if(esp_state.uart_scramble) /* wrong UART settings? scramble the data(not modeling the nearly analog nature of real framing errors...) */
	///	val = ((val * val) & 0b01010101);

	if((esp_state.state & ESP_ECHO) && esp_state.user_input_mode < ESP_USER_MODE_UNVARNISHED){ /* should we echo the byte back? */
		cu_esp_txp((const char *)&val);
//print_message("ECHO [(const char *)&val]\n");
	}

	if(esp_state.user_input_mode == ESP_USER_MODE_UNVARNISHED){ /* sending time window mode? */

		esp_state.unvarnished_bytes++; /* keep track of how many bytes to send out, once the timing window closes */
		if(cycle < esp_state.unvarnished_end_cycle && (esp_state.unvarnished_end_cycle - cycle) > 100000UL) /* rolled over? */
			esp_state.unvarnished_end_cycle = cycle;

		if(cycle >= esp_state.unvarnished_end_cycle){ /* time to send data? */

			if(esp_state.unvarnished_bytes == 3){ /* check special case */
				if(esp_state.write_buf[0] == '+' && esp_state.write_buf[1] == '+' && esp_state.write_buf[2] == '+'){/* single timing window containing only "+++"?, then break out of unvarnished mode */
					print_message("ESP Broke out of transparent/unvarnished mode\n");
					esp_state.user_input_mode = ESP_USER_MODE_AT;
					return;
				}
			}
			esp_state.unvarnished_end_cycle = WRAP32(cycle + ESP_UNVARNISHED_DELAY); /* set next end event */
			cu_esp_net_send_unvarnished(esp_state.write_buf, esp_state.unvarnished_bytes);
			esp_state.write_buf_pos_in = 0;
			esp_state.unvarnished_bytes = 0;
		}else{
			print_message("WAIT UNVARNISHED\n");
		}

	}else if(esp_state.user_input_mode == ESP_USER_MODE_SEND){ /* send fixed length mode(prefixed with send command)? */
		esp_state.remaining_send_bytes--;
		if(esp_state.remaining_send_bytes == 0){ /* last byte received for a send? */
		//	if(cu_esp_net_send(esp_state.write_buf) == 0)
		//		cu_esp_txp((const char*)"SEND OK\r\n");
		//	else
		//		cu_esp_txp((const char*)"ERROR\r\n");

			esp_state.user_input_mode = ESP_USER_MODE_AT;	
		}

	}else if(esp_state.user_input_mode == ESP_USER_MODE_AT){

		if(val == '\n' && esp_state.write_buf_pos_in > 1 && esp_state.write_buf[esp_state.write_buf_pos_in-2] == '\r'){ /* AT mode, and something that ends with "\r\n"? */
//print_message("AT: %d\n", esp_state.write_buf_pos_in);
			esp_state.write_buf[esp_state.write_buf_pos_in] = '\0'; /* terminate it as a string */
			cu_esp_process_at(esp_state.write_buf);
			esp_state.write_buf_pos_in = 0UL;
			esp_state.write_buf[0] = '\0';
		}
	}else{ /* ESP_USER_MODE_PASSTHROUGH serial/TTY passthrough mode, interface with physical UART on the host machine */

	}
}


auint cu_esp_update_timer_counts(auint cycle){

	uint8 i;
	auint tdelta;
//	auint earliest = cycle;//earliest time another bytes by can be transmitted, based on all delay timers
	auint last_available = esp_state.read_buf_pos_out;//based on all delay timers, last position in buffer that could be used yet
	if(cycle > esp_state.last_cycle_tick)
		tdelta = WRAP32(cycle - esp_state.last_cycle_tick);
	else
		tdelta = WRAP32(esp_state.last_cycle_tick - cycle);

	esp_state.last_cycle_tick = cycle;/* keep track of the last tick cycle for timing logic */
//print_message("%d\n", tdelta);
	if(esp_state.ip_delay_timer){ /* process timer for getting an IP after joining an AP */
//print_message("CYCLE: %d, LAST: %d, DELAY %d\n", cycle, esp_state.last_cycle_tick, esp_state.ip_delay_timer);
		if(esp_state.ip_delay_timer <= tdelta){
			esp_state.ip_delay_timer = 0;
			print_message("ESP: Wifi Got IP\n");
			cu_esp_txp((const char *)"WIFI GOT IP\r\n");
		}else
			esp_state.ip_delay_timer -= tdelta;
	}

	if(esp_state.join_delay_timer){ /* process timer between the command to connect to an AP, and the actual completed action */
		if(esp_state.join_delay_timer <= tdelta){
			esp_state.join_delay_timer = 0;
			char ap_buffer[64+64];
			sprintf(ap_buffer,"+CWJAP:%s\r\nOK\r\n",esp_state.wifi_name);
			cu_esp_txp(ap_buffer);
		}else
			esp_state.join_delay_timer -= tdelta;
	}

return last_available;
///////////////////////////////////////////////



	for(i=0; i<sizeof(esp_state.delay_pos); i++){ /* check each delay slot, and see if the timer needs to be updated */
		if(!esp_state.delay_len[i])
			continue;


		if(esp_state.delay_len[i] <= tdelta){
			esp_state.delay_len[i] = 0;
			continue;
		}
		/* see if this timer is preventing further UART data to be transferred */
		esp_state.delay_len[i] -= tdelta;
		if(esp_state.delay_pos[i] < last_available)
			last_available = esp_state.delay_pos[i];
		
	}

	return last_available;
}


auint cu_esp_uzebox_read_ready(auint cycle){ /* host side checking if a byte has been received */

	if(esp_state.read_ready_cycle > cycle && (esp_state.read_ready_cycle - cycle) > 100000UL){ /* rolled over? */
	print_message("ROLLED OVER\n");
sleep(5);
		esp_state.read_ready_cycle = cycle;
	}

	if(cycle >= esp_state.read_ready_cycle){
//print_message("R\n");
		esp_state.read_ready_cycle = 0UL;

		auint buf_next = cu_esp_update_timer_counts(cycle);/* based on timing, get the max position we can send from */

		if(esp_state.read_buf_pos_in != esp_state.read_buf_pos_out){ /* data is available? */

			if(buf_next == esp_state.read_buf_pos_out) /* no timer delays prevent this data being sent yet? */
				return (1<<RXC0);
			else{
				print_message("<<<<STALL>>>>\n");
				return 0; /* some timer is preventing further output, it likely pre-queued a delayed message that is not to be sent yet */
			}
		}else if(esp_state.state & ESP_INTERNET_ACCESS){ /* no data queued, see if there is new net data received that we should send now(otherwise let it buffer until Uzebox checks) */

			if(cu_esp_process_ipd() > 0){
				//print_message("GOT NETWORK DATA\n");
				return (1<<RXC0);
			}
		}
	}
//print_message("N\n");
	return 0;
}


auint cu_esp_uzebox_read(auint cycle){ /* host side has attempted to receive a UART byte */

	if(esp_state.read_buf_pos_out == esp_state.read_buf_pos_in){ /* no new data? return the last byte sent */
		print_message("ESP UART: Uzebox read, but no data is buffered\n");
		return esp_state.last_read_byte;
	}

	auint val = esp_state.read_buf[esp_state.read_buf_pos_out++];
	if(esp_state.read_buf_pos_out >= sizeof(esp_state.read_buf))
		esp_state.read_buf_pos_out = 0UL;

	if(esp_state.read_buf_pos_in == esp_state.read_buf_pos_out){ /* that was the last byte ready? */
		esp_state.read_buf_pos_in = esp_state.read_buf_pos_out = 0UL;
		esp_state.last_read_byte = val; /* keep track of this, so we can send it if a byte is read but we have nothing buffered */
	}

//	print_message("ESP uzebox read %d(%c), cycle %d\n", val, val, cycle);
	esp_state.read_ready_cycle = WRAP32(cycle + esp_state.uart_baud_bits); /* uart_baud_bits is recalculated everytime UART flags are changed */

	if(esp_state.uart_rx_enabled){ /* we need to process UART even if Uzebox wont received it, to keep timing behavior correct for network data, etc */

//		if(esp_state.uart_scramble) /* wrong UART settings? scramble the data(not modeling the nearly analog nature of real framing errors...) */
//			val = ((val * val) & 0b01010101);

		return val;
	}
	return 0;
}




void cu_esp_uzebox_modify(auint port, auint val, auint cycle){ /* host side has modified UART settings */

print_message("\tESP uzebox modify UART, cycle: %d, port: %d, val: %d\n", cycle, port, val);

	if(port == CU_IO_UCSR0A){ /* UART double speed mode? */

		esp_state.uart_synchronous = ((val & (3<<UMSEL00))>>UMSEL00);
		esp_state.uart_double_speed = ((val & (1<<U2X0))>>U2X0);
		esp_state.uart_ucsr0a = val;

	}else if(port == CU_IO_UCSR0B){ /* Rx/Tx on/off */

		esp_state.uart_tx_enabled = ((val & (1<<TXEN0))>>TXEN0);
		esp_state.uart_rx_enabled = ((val & (1<<RXEN0))>>RXEN0);
		esp_state.uart_ucsr0b = val;

	}else if(port == CU_IO_UCSR0C){ /* frame settings(ie 8N1) */

		esp_state.uart_data_bits = 5+((val & (3<<UCSZ00))>>UCSZ00);
		esp_state.uart_stop_bits = 1+((val & (1<<USBS0))>>USBS0);
		esp_state.uart_parity = ((val & (3<<UPM00))>>UPM00); 
		esp_state.uart_ucsr0c = val;

	}else if(port == CU_IO_UBRR0L){ /* controls baud bits 0-7 */

		esp_state.uart_ubrr0l = (val & 0xFF);
		esp_state.uart_baud_bits &= ~0xFF; /* reset bits 0-7 */
		esp_state.uart_baud_bits |= esp_state.uart_ubrr0l; /* set new value */

	}else if(port == CU_IO_UBRR0H){ /* controls baud rate bits 8-11, among other things */
		//if(!(val & (1 << URSEL))) /* URSEL must be 0 to set UBRR0H? */
		esp_state.uart_ubrr0h = (val & 0x0F);
		esp_state.uart_baud_bits &= ~(0x0F00); /* reset bits 8-11 */
		esp_state.uart_baud_bits |= (esp_state.uart_ubrr0h<<8);

	}

	esp_state.uart_scramble = 0; /* don't scramble UART data unless there is a bad setting */

	if(esp_state.uart_synchronous){ /* semi realistic? */
		print_message("\t\tESP UART broken: synchronous mode instead of asynchronous\n");
		esp_state.uart_scramble = 1;
		esp_state.uart_tx_enabled = 0;
		esp_state.uart_tx_enabled = 0;
	}

	if(!esp_state.uart_baud_bits || (esp_state.uart_baud_bits != esp_state.uart_baud_bits_module) || (esp_state.uart_data_bits != 8) || (esp_state.uart_stop_bits != 1) || (esp_state.uart_parity != 0)){ /* partially realistic, characters are scrambled if settings are off, framing errors are too difficult/useless to model */
		print_message("\t\tESP UART broken: faking frame errors\n");
		esp_state.uart_scramble = 1;
	}

	if(!esp_state.uart_tx_enabled || !esp_state.uart_rx_enabled){
		print_message("\t\tESP UART broken: TxE: %d, RxE: %d\n", esp_state.uart_tx_enabled, esp_state.uart_rx_enabled); 
	}

	print_message("\t\tESP UART Registers: UCSR0A=0x%02x, UCSR0C=0x%02x, UCSR0C=0x%02x\n", esp_state.uart_ucsr0a, esp_state.uart_ucsr0b, esp_state.uart_ucsr0c);
	print_message("\t\tESP UART State: Tx:%d, Rx:%d, Ubaud:%d, Ebaud:%d, Stop:%d, Parity:%d, Data:%d, Sync:%d\n", esp_state.uart_tx_enabled, esp_state.uart_rx_enabled, esp_state.uart_baud_bits, esp_state.uart_baud_bits_module, esp_state.uart_stop_bits, esp_state.uart_parity, esp_state.uart_data_bits, esp_state.uart_synchronous); 
	if(esp_state.uart_tx_enabled && esp_state.uart_rx_enabled && !esp_state.uart_scramble)
		print_message("\t\tESP ^ UART GOOD ^\n");
}


auint cu_esp_net_last_error(){
	return 0;//return errno;
}


auint cu_esp_atoi(char *str){
	return atoi(str);
}


void cu_esp_reset_pin(uint8 state, auint cycle){

	if(state && esp_state.reset_pin) /* We are still operating */
		return;

	if(!state){ /* Can't operate when RST pin grounded */

		if(esp_state.reset_pin){ /* just got reset? */

			esp_state.reset_pin = 0;
			esp_state.last_cycle_tick = cycle;
			cu_esp_reset_uart();
			cu_esp_reset_network();
			print_message("ESP: Shutdown\n");
		}/* else held reset */

	}else if (!esp_state.reset_pin){ /* RST pin just got released, boot */
		esp_state.reset_pin = 1;
		cu_esp_load_config();
		cu_esp_reset_uart(); /* clear buffers and set default baud bits/divisor */
	//	if (cu_esp_init_sockets() == ESP_SOCKET_ERROR){

	//		esp_state.state &= ~ESP_INTERNET_ACCESS;
	//		print_message("ESP ERROR: failed to initialize sockets\n");
	//	}
//		cu_esp_timed_stall(WRAP32(cycle + ESP_RESET_BOOT_DELAY)); /* this will delay UART output until a semi-realistic boot delay has happened */ 
//		esp_state.wifi_timer = 1;
//		esp_state.wifi_delay = ESP_AT_CWJAP_DELAY;
		cu_esp_txp(start_up_string);
		print_message("ESP: Start\n");
	}
}


sint32 cu_esp_init_sockets(){

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	if(!esp_state.winsock_enabled){ /* this only needs to be done once per program run */
		esp_state.winsock_enabled = 1;
		WSADATA WSAData;
		if (WSAStartup(MAKEWORD(2,2),&WSAData)){
			print_message("ESP ERROR: Failed for Winsock 2.2 reverting to 1.1\n");
			if (WSAStartup(MAKEWORD(1,1),&WSAData)){
				print_message("ESP ERROR: Failed on WSAStartup(): %d",cu_esp_get_last_error());
				return ESP_SOCKET_ERROR;
			}
		}
	}
#endif

	return 0;
}

void cu_esp_reset_network(){
	cu_esp_init_sockets(); /* prepare sockets, if necessary(winsock) */
	for (sint32 i=0;i<4;i++){
		esp_state.socks[i] = ESP_INVALID_SOCKET;
		esp_state.proto[i] = ESP_INVALID_SOCKET;
	}

	esp_state.ip_delay_timer = ESP_AT_IP_DELAY;
}





uint16 cu_esp_checksum(void *b, sint32 len){ /* Standard 1s complement checksum */

	sint16 *buf = b;
	uint32 sum=0;
	uint16 result;
	for (sum=0;len>1;len-=2)
		sum += *buf++;
	if (len==1)
		sum += *(unsigned char*)buf;
	sum = (sum >> 16) + (sum & 0xFFFF);
	sum += (sum >> 16);
	result = ~sum;
	return result;
}


void cu_esp_at_cwsap(sint8 *cmd_buf){ /* Get or set wifi credentials for softAP */

	if (!strncmp((char*)&cmd_buf[3+5],"?\r\n",3) || !strncmp((char*)&cmd_buf[3+5],"_CUR?\r\n",7) || !strncmp((char*)&cmd_buf[3+5],"_DEF?\r\n",7)){ /* Query only */

		char sapbuffer[256];
		if (!strncmp((char*)&cmd_buf[3+5],"_DEF?\r\n",7)){ /* Must read default saved version in case current version is different */
		/* TODO TODO */
		
		}else
			sprintf(sapbuffer,"\"%s\",\"%s\",%d,%d\r\n",esp_state.soft_ap_name,esp_state.soft_ap_pass,esp_state.soft_ap_channel,esp_state.soft_ap_encryption);
print_message("\nCWSAP:%s\n",sapbuffer);
		cu_esp_txp(sapbuffer);
		cu_esp_txp_ok();
		return;
	}

	sint32 at_error = 0;
	sint32 i;

	/* TODO SUPPORT _CUR AND _DEF */
	if (cmd_buf[3+6] != '"'){ /* Must start with '"' */
			at_error = 1;
		}else{
			for (i=(3+7);i<(3+7)+64;i++){
				if (cmd_buf[i] == '"'){ /* Got terminating '"' */
					continue;
				}
				if (i == (3+7)+64-1){ /* Too long or wasn't properly terminated */
					at_error = 2;
					break;
				}
			}
		
		}
		if (at_error){
			cu_esp_txp_error();
			return;
		}
//		sprintf(esp_state.soft_ap_name,"%s",(char *)&cmd_buf[3+8]);
//		sprintf(esp_state.soft_ap_pass,"%s",(char *)&cmd_buf[3+8]);
		/* Don't care, we will just let it work for now */
		cu_esp_txp_ok();
}


void cu_esp_at_cipmode(sint8 *cmd_buf){ /* Set the packet send mode */

	/* Command does not support query in any firmware? */
	if (cmd_buf[11] < '0' || cmd_buf[11] > '1')
		cu_esp_txp_error();
	else
		esp_state.cip_mode = cmd_buf[11] - '0';
	
	cu_esp_txp_ok();
}


void cu_esp_at_cwlap(sint8 *cmd_buf){ /* List all available WiFi access points */
	
	esp_state.state |= ESP_LIST_APS; /* TODO WHY ??? */
	char ap_buffer[128];
//	uint32 ap_pos = 0;
	sint32 i;

	cu_esp_timed_stall(ESP_AT_CWLAP_DELAY);
	for (i=0;i<ESP_NUM_FAKE_APS;i++){
		sprintf(ap_buffer,"+CWLAP:(1,\"%s\",-%i,\"%02x:%02x:%02x:%02x:%02x:%02x\")\r\n",fake_ap_name[i],rand()%100,rand()%256,rand()%256,rand()%256,rand()%256,rand()%256,rand()%256);
		cu_esp_txp(ap_buffer);
		cu_esp_timed_stall(ESP_AT_CWLAP_INTER_DELAY);
	}
asm volatile("": : :"memory"); /* Memory fence */
	cu_esp_txp_ok();
}


void cu_esp_at_cipsend(sint8 *cmd_buf){ /* Send a packet */


	if (!strncmp((char*)&cmd_buf[3+7],"?\r\n",3)){ /* Query only */
			
		if ( (esp_state.state & ESP_AP_CONNECTED) && (esp_state.socks[0] != ESP_INVALID_SOCKET))
			cu_esp_txp_ok(); /* Sends should succeed */
		else{ /* No way we could send a packet */
			cu_esp_txp_error();
		}

		return;

	}else if (cmd_buf[3+7] == '\r' && cmd_buf[3+8] == '\n'){ /* Enter unvarnished transmission mode */
//TODO MAKE SURE WE ARE CONNECTED TO SOMETHING
print_message("ESP Starting Transparent Transmission\n");
		if (0)//esp_state.cip_mode == 1)
			cu_esp_txp_error();
		else{
			esp_state.user_input_mode = ESP_USER_MODE_UNVARNISHED;
			esp_state.unvarnished_end_cycle = 0;
			esp_state.read_buf_pos_in = esp_state.read_buf_pos_out = 0UL;
			cu_esp_txp((const char *)">");
		}
		return;

	}else{ /* Send */

		sint32 connection_num = 0; /* When not MUX, the only active socket is 0 */

		if (esp_state.state & ESP_MUX){ /* Multiple connections mode, must specify connection number */

			connection_num = cmd_buf[3+8]-'0';
			if (connection_num < 0 || connection_num > 3){
				cu_esp_txp_error();
	print_message("BAD CONNECTION NUM\n");
				return;
			}

		}else
			print_message("send not MUX\n");

		esp_state.send_to_socket = connection_num; /* Store this so we know what socket to set the payload to later */

		if (!(esp_state.state & ESP_AP_CONNECTED) || (esp_state.socks[connection_num] == ESP_INVALID_SOCKET)){ /* Can't send, not connected.(What if we are acting as AP?) */
				
			if (!(esp_state.state & ESP_AP_CONNECTED)){
				/*if (esp_state.debug_level > 0)	print_message("Can't SEND, not connected to the AP",0); */
			}else{
				/*if (esp_state.debug_level > 0)	print_message("Can't SEND, no connection on socket:",connection_num); */
			}
			cu_esp_txp_error();
			
			return;

		}

		/* if (esp_state.debug_level > 0)	print_message("CIPSEND=",0); */
		sint8 i = cmd_buf[3+8];
		if ( (esp_state.state & ESP_AP_CONNECTED)){ /* Must be connected to AP? or what if AP?? */
				
			if (i >= '0' && i < '4' && cmd_buf[3+9] == ','){
					
				sint32 s = cmd_buf[3+8] - '0'; /* Get the socket we will use */
				if (!(esp_state.state & ESP_MUX) && s > 0){
					/* if (esp_state.debug_level > 0)	print_message("CIPSEND= cant use connection > 0 without MUX=1",0); */
					
				}else{
					/* if (esp_state.debug_level > 0)	print_message("CIPSEND good connection num",s); */
				}

				if (esp_state.socks[s&3] == ESP_INVALID_SOCKET){ /* Error, not connected on this socket */
					/* if (esp_state.debug_level > 0)	print_message("Cant send, not connected on socket",s); */

				}else{ /* Socket is good, continue checking format */

					sint32 bad_format = 0;
					for (i=3+10;i<3+10+4;i++){ /* Maximum 4 digit number of bytes to send, in base 10 */
						if (cmd_buf[i] >= '0' && cmd_buf[i] <= '9'){

						}else if (i == 3+10 || cmd_buf[i] != '\r'){ /* Must be >= 1 byte, if not a base 10 number, then must be start of "\r\n" */
							bad_format = 1;
						}else{ /* It's '\r', replace it with a '\0' so Atoi() will work, we will have to replace it with \r later so clean up works */
							cmd_buf[i] = '\0'; /* Must replace with \r later, we keep the value of i after the for loop for this */
							break;
						}
					}
					if (bad_format){ /* Either terminated with no length specified, or got garbage characters before "\r\n" termination */
						/* if (esp_state.debug_level > 0)	print_message("CIPSEND=Bad format",0); */

					}else{ /* Format passed, get ready to receive the specified number of bytes(must be > 0) */

						esp_state.rx_await_bytes = cu_esp_atoi((char *)&cmd_buf[3+10]); /* We terminated it with '\0' above */
						/* if (esp_state.debug_level > 0)	print_message("CIPSEND format good, waiting for bytes:",esp_state.rx_await_bytes); */
			cmd_buf[i] = '\r'; /* Now we replace the '\0' with '\r' so this command can be removed properly */

			/* TODO DO WE HAVE TO EAT THE BYTES OR JUST UPDATE THE BUFFER POS TO READ FROM?!?!?! */

						if (esp_state.rx_await_bytes){

                                	esp_state.send_to_socket = s; /* Keep track of which socket we will be sending on after waiting for input */
							esp_state.state |= ESP_AWAITING_SEND; /* Next time around, we will receive the bytes */
							cu_esp_txp((const char*)">");
						}else{ /* Atoi says the string == 0 */

						}
					} /* Format passed */
				} /* Socket is good */
			}else{ /* Bad format */
				/* if (esp_state.debug_level > 0)	print_message("CIPSEND bad connection num",0); */

			}
		}else{ /* Not connected to AP */
			if (esp_state.socks[0] == ESP_INVALID_SOCKET){

			}
		}
	} /* Send mode(not query only mode) */
}


void cu_esp_at_cwjap(sint8 *cmd_buf){ /* Join a wifi access point, fake, wont use host hardware to make this real */
print_message("ESP Join AP\n");
	uint8 at_error = 0;
	sint32 i;
	if (0 && strncmp("?\r\n",(const char *)&cmd_buf[8],3)){ /* Query only */

		if (esp_state.state & ESP_AP_CONNECTED){
			char ap_buffer[64+64];
			sprintf(ap_buffer,"+CWJAP:%s\r\nOK\r\n",esp_state.wifi_name);
			cu_esp_txp(ap_buffer);
		}else{
			cu_esp_txp((const char *)"ERROR\r\n");
print_message("jap failed?!?");
		}

	}else{ /* Check the credentials against the fake APs */

		/* TODO HACK */

		cu_esp_timed_stall(ESP_AT_CWJAP_DELAY);
		esp_state.ip_delay_timer = ESP_AT_IP_DELAY;
		cu_esp_txp_ok();
		return;
		if (cmd_buf[3+6] != '"'){ /* Must start with '"' */
			at_error = 1;
		}else{
			for (i=(3+7);i;i++){
				if (cmd_buf[i] == '"'){ /* Got terminating '"' */
					break;
				}
				if (i == (3+6)+32){ /* SSID too long or wasn't properly terminated */
					at_error = 2;
					break;
				}
			}
			if (!at_error && cmd_buf[++i] != ',')
				at_error = 3;
			else if (!at_error && cmd_buf[++i] != '"')
				at_error = 4;
			else{ /* Check for valid password */
				for (;i;i++){
					if (cmd_buf[i] == '"'){ /* Got terminating '"' */
						break;
					}
					if (i == (3+6)+32){ /* Password too long or wasn't properly terminated */
						at_error = 5;
						break;
					}
				}
			}

		
		}
		if (at_error){
			cu_esp_txp_error();
			return;
		}
		sprintf((char *)esp_state.wifi_name,"%s",(char *)&cmd_buf[3+8]);
		sprintf((char *)esp_state.wifi_pass,"%s",(char *)&cmd_buf[3+8]);
		esp_state.flash_dirty = 1; /* Make sure this gets saved */
		esp_state.wifi_timer = 1;
		esp_state.wifi_delay = ESP_AT_CWJAP_DELAY+((rand()%1000)*ESP_AT_MS_DELAY);

		cu_esp_txp_ok();
		}

}


void cu_esp_at_cwmode(sint8 *cmd_buf){

	if (!strncmp((char*)&cmd_buf[9],"?\r\n",3)){ /* Query which mode we are in */
		
		if (esp_state.uart_at_mode == 1) /* STA */
			cu_esp_txp((const char*)"CWMODE:1\r\nOK\r\n");
		else if (esp_state.uart_at_mode == 2) /* AP */
			cu_esp_txp((const char*)"CWMODE:2\r\nOK\r\n");
		else /* STA+AP */
			cu_esp_txp((const char*)"CWMODE:3\r\nOK\r\n");

	}else if (cmd_buf[9] == '=' && !strncmp((char*)&cmd_buf[11],"\r\n",2)){ /* Set the mode we are in */

		sint32 mt = cmd_buf[3+7];

		if (mt < '1' || mt > '3'){ /* Bad format */

			/* if (esp_state.debug_level > 0)	print_message("bad CWMODE format must be 1,2, or 3",0); */
			cu_esp_txp_error();
			
			return;

		}else{
			/* TODO SAVE THIS TO FLASH?!? */
			cu_esp_txp_ok();
			mt -= '1';
			esp_state.uart_at_mode = mt;

		}

	}else if (!strncmp((char*)&cmd_buf[9],"=?\r\n",4)){ /* Ask what modes are possible(dubious but some firmwares support this??) */
		cu_esp_txp((const char*)"+CWMODE:(1-3)\r\nOK\r\n"); 

	}else if (!strncmp((char*)&cmd_buf[9],"_CUR=",5 && cmd_buf[15] == '\r' && cmd_buf[16] == '\n')){ /* Set the mode but do not save as default to flash */

		sint32 mt = cmd_buf[3+11];

		if (mt < '1' || mt > '3'){ /* Bad format */

			cu_esp_txp_error();
			return;

		}else{

			cu_esp_txp_ok();
			mt -= '1';
			esp_state.uart_at_mode = mt;
		}

	}else{ /* Bad format */
		cu_esp_txp_error();
	}
}


void cu_esp_at(){
	cu_esp_txp_ok();
}


void cu_esp_at_ate(sint8 *cmd_buf){

	if (!strncmp("0\r\n",(const char*)&cmd_buf[3],3)){ /* Turn echo off */
		/* cu_esp_timed_stall(ESP_AT_MS_DELAY*2000); */
		cu_esp_txp_ok();
//cu_esp_txp("WIFI GOT IP\r\n");
		esp_state.state &= ~ESP_ECHO;

	}else if (!strncmp("1\r\n",(const char*)&cmd_buf[3],3)){ /* Turn echo on */
		cu_esp_txp_ok();
		esp_state.state |= ESP_ECHO;

	}else if (!strncmp("?\r\n",(const char*)&cmd_buf[3],3)){ /* Query only(is this supported?) */
		cu_esp_txp_ok();
	}else{ /* Bad format */
		cu_esp_txp_error();
	}

	return;
}


void cu_esp_at_cipstart(sint8 *cmd_buf){
esp_state.state |= ESP_AP_CONNECTED | ESP_INTERNET_ACCESS;//HACK
//	esp_state.state |= ESP_MUX; /* TODO hack */

//	sint32 at_error = 0;
	if (!strncmp((char*)&cmd_buf[3+9],"?\r\n",3)){ /* Query only(supported on any firmware??) */
		/* TODO check that we could send */
		if (esp_state.socks[0] != ESP_INVALID_SOCKET)
			cu_esp_txp_ok();
		
		if (esp_state.state & ESP_AP_CONNECTED) /* Possible to start a connection? */
			cu_esp_txp_ok();
		else
			cu_esp_txp_error();
		return;

	}

	/* Start connection, check AT format */
	sint8 s = cmd_buf[12]; /* AT+CIPSTART=x */
//	uint32 proto;
	uint32 off = 0;

	if (esp_state.state & ESP_MUX){ /* Get the connection number and check that it is valid */
//print_message("ARE IN MUX\n");
//sleep(4000);
		if (s < '0' || s > '4' || cmd_buf[13] != ','){ /* Bad connection number or missing comma */
			cu_esp_txp_error();
			return;
		}
		
		s -= '0';
		if (esp_state.socks[s] != ESP_INVALID_SOCKET){ /* Socket is already in use */
			cu_esp_txp_error();
			return;
		}
		off = 2;
	}else if (cmd_buf[12] != '"'){ /* When not MUX, string should be "AT+CIPSTART="TCP".." or "AT+CIPSTART="UDP",.." */
		s = 0;
		
			//cu_esp_txp_error();
			//return;
	}

	/* We are clear to use this connection number, or if !MUX none was given, continue checking format */
	sint32 type = 0;

	if (!strncmp((char*)&cmd_buf[3+9+off],"\"TCP\",",6)){ /* Open a TCP connection */

		type = ESP_PROTO_TCP;
	}else if (!strncmp((char*)&cmd_buf[3+9+off],"\"UDP\",",6)){ /* Open a UDP "connection" */

		type = ESP_PROTO_UDP;
	}else{
		cu_esp_txp_error();
		return;				
	}
	
	esp_state.protocol[s] = type;
	/* Check the hostname given */
	sint8 hostname[128+1];
	sint32 hlen = 0;
	sint32 i;
	for (i=16+off;i<(sizeof(hostname)-1)+16+off;i++){
		if (cmd_buf[3+i] == ','){
				hostname[i] = '\0'; /* TODO------------------------------ */
				break;
			}
			hostname[hlen++] = cmd_buf[3+i];
		}
	if (!hlen){
		cu_esp_txp_error();
		return;
	}
			
	hostname[hlen-1] = '\0';
	sint32 port = cu_esp_atoi((char *)&cmd_buf[3+i+1]); /* 50697 */
					 
	/* hostname[i-1] = '\0'; remove trailing '"' */
s = 0;
type = ESP_PROTO_TCP;
	i = cu_esp_net_connect(hostname,s,port,type);
//i = 0;	
	if (!i){ /* Connection success */
		cu_esp_txp((const char*)"OK\r\nLinked\r\n");
		return;
	}else{ /* Connection failed(doesn't exist, server down, etc) */
		cu_esp_txp_error();
		return;
	}

				

}


void cu_esp_at_ipr(sint8 *cmd_buf){ /* DEPRECIATED */

	auint narg;
	sscanf((char *)&cmd_buf+7, "%d", (int *)&narg); /* extract the numerical argument */
	sprintf((char *)&cmd_buf, "AT+CIOBAUD=%d\r\n", narg); /* convert to new representation */
	cu_esp_at_ciobaud(cmd_buf); /* run as normal */
}


void cu_esp_at_cipclose(sint8 *cmd_buf){
	
	if (!(esp_state.state & ESP_MUX)){ /* Single connection mode? then there is only 1 possible command format */
	
		if (cmd_buf[11] != '\r' || cmd_buf[12] != '\n')
			cu_esp_txp_error();
		else
			cu_esp_txp((const char*)"CLOSED\r\n");
		return;

	}else if (cmd_buf[11] != '='){ /* Multiple connection mode */
		cu_esp_txp_error();
	}


	sint8 i = cmd_buf[12];

	if (i >= '0' && i < '5'){
		i -= '0';
		if (!(esp_state.state & ESP_MUX) && i > 0){
			cu_esp_txp_error(); /* TODO does it return error or just OK(since it would already be closed) */
		}else{
			cu_esp_close_socket(i);
			cu_esp_txp((const char*)"CLOSED\r\n");
		}
	}else if (i == '5' && (esp_state.state & ESP_MUX)){ /* Close all connections */
		for (uint32 i=0;i<4;i++)
			cu_esp_close_socket(i);
		cu_esp_txp((const char*)"CLOSED\r\n");
		
	}else{
		/* Bad connection number, TODO what about listen()?? */
	}

}


void cu_esp_at_cipstatus(sint8 *cmd_buf){
	
	if (!strncmp((const char *)&cmd_buf[12],"=?\r\n",4)){ /* This command does nothing? TODO check against other documentation */
		cu_esp_txp_ok();
		return;
	}
	
	if (cmd_buf[12] != '\r' && cmd_buf[13] != '\n'){
		cu_esp_txp_error();
		return;
	}

	cu_esp_txp((const char *)"status:");

	for (uint32 i=0;i<4;i++){
		if (esp_state.socks[i] == ESP_INVALID_SOCKET)
			continue;
	
		cu_esp_txp((const char *)"+CIPSTATUS:");
		cu_esp_txi(i);
	
		if (esp_state.protocol[i] == ESP_PROTO_UDP)
			cu_esp_txp((const char *)",\"UDP\",");
		else
			cu_esp_txp((const char *)",\"TCP\",");
	
		cu_esp_txp((const char *)"127.0.0.1,"); /* TODO print real IP address for connection */
		cu_esp_txp((const char *)"1000"); /* TODO print real port for connection */
		cu_esp_txp((const char *)"0"); /* TODO print real role(server or client) for this connection */
		cu_esp_txp((const char *)"\r\n");
	}
	cu_esp_txp_ok();
}


void cu_esp_at_cipserver(sint8 *cmd_buf){
 /* Note that this command is TCP only, to listen for UDP packets you just need to "AT+CIPSTART=0,"UDP",999" and have something send data to the port(connectionless) */

/*
1. Server can only be created when AT+CIPMUX=1
2. Server monitor will automatically be created when Server is created.
3. When a client is connected to the server, it will take up one connection?and be provided an ID
TODO*/


	char c = cmd_buf[3+11];

	if (cmd_buf[3+10] != '=' || c < '0' || c > '3' || cmd_buf[3+12] != ',' || cmd_buf[3+13] < '1' || cmd_buf[3+13] > '9'){ /* Bad format */
		cu_esp_txp_error();
		return;
	}
	
	sint32 p = cu_esp_atoi((char *)&cmd_buf[3+13]);
	
	if (!p || p > 65535){
		cu_esp_txp_error();
		return;
	}

	uint32 sock=0;
	for (;sock<4;sock++){ /* Look for a free socket */

	}
	if (sock > 3){ /* No free socket */
		cu_esp_txp_error();
		return;
	}

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	u_long block_mode = 1;
	if (ioctlsocket(esp_state.socks[sock],FIONBIO,&block_mode)){

#else

	auint block_mode = 1;
	if (ioctl(esp_state.socks[sock],FIONBIO,&block_mode)){


/* is this better? fcntl(server_socket, F_SETFL, O_NONBLOCK); */
/*	int ssoe = setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (void *)&enable, sizeof(enable));//allow addr reuse */
/* 	ssoe = setsockopt(server_socket, SOL_SOCKET, SO_REUSEPORT, (void *)&enable, sizeof(enable));//allow port reuse */
/* see auto-es for other ideas... */
#endif
//hack disable check for Emscripten
//		print_message("ESP ERROR: failed to set non-blocking mode: %d\n", cu_esp_get_last_error());
//		return;
	}
	//listen(sock);

	cu_esp_close_socket(sock);
/*
	i = CreateSocket(i,ESP_PROTO_TCP,p);
	sockaddr_in server;
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = inet_addr("127.0.0.1");
	server.sin_port = htons(27015);
    if (bind(i,
             (SOCKADDR *) & service, sizeof (service)) == ESP_SOCKET_ERROR) {
        wprintf(L"bind failed with error: %ld\n", cu_esp_get_last_error());
        cu_esp_close_socket(esp_state.listen_socket);
        WSACleanup();
        return 1;
    }

    // Listen for incoming connection requests. 
    // on the created socket 
    if (listen(esp_state.listen_socket, 1) == ESP_SOCKET_ERROR) {
        wprintf(L"listen failed with error: %ld\n", cu_esp_get_last_error());
        closesocket(esp_state.listen_socket);
        WSACleanup();
        return;
    }
*/


/* ALTERNATE NEW VERSION
	command_addr_in.sin_family = AF_INET;//using IPv4 TCP
	command_addr_in.sin_port = htons(command_port);
	command_addr_in.sin_addr.s_addr = INADDR_ANY;//use whatever interface the OS thinks

	errno = 0;	
	if((command_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		print_message("**ERROR** failed to create command socket, errno %d\n", errno);
		return -1;
	}
	fcntl(command_socket, F_SETFL, O_NONBLOCK);//make it non-blocking, so we can poll for data

	int enable = 1;
	int ssoe = setsockopt(command_socket, SOL_SOCKET, SO_REUSEADDR, (void *)&enable, sizeof(enable));//allow addr reuse
	if(ssoe){
		print_message("Can't setup command reuse address option, errno %d\n", errno);
		return -1;
	}
	ssoe = setsockopt(command_socket, SOL_SOCKET, SO_REUSEPORT, (void *)&enable, sizeof(enable));//allow port reuse
	if(ssoe){
		print_message("Can't setup command reuse port option, errno %d\n", errno);
		return -1;
	}
		
	if(bind(command_socket, (struct sockaddr *)&command_addr_in, sizeof(command_addr_in)) == -1){
		print_message("**ERROR** command network bind() failed, errno %d\n", errno);
		return -1;
	}

	if(listen(command_socket, 10) == -1){	
		print_message("**ERROR** command network listen() failed, errno %d\n", errno);
		return -1;
	}

*/
	esp_state.server_timer = 1; /* This will start counting up until specified time limit("AT+CIPSTO") */
	cu_esp_txp_ok();
}


void cu_esp_at_cipmux(sint8 *cmd_buf){
 /* This mode can only be changed after all connections are disconnected. If server is started, reboot is required TODO */

	/* sint32 at_error = 0; */
	if (!strncmp((char*)&cmd_buf[9],"?\r\n",3)){ /* Query only */

		if (esp_state.state & ESP_MUX)
			cu_esp_txp((const char*)"MUX is 1\r\n");
		else
			cu_esp_txp((const char*)"MUX is 0\r\n");
		return;

	}else if (cmd_buf[3+6] == '='){ /* Setting mode */
		if (!strncmp((char*)&cmd_buf[3+8],"\r\n",2)){ /* Properly terminated? */

			if (cmd_buf[3+7] == '0'){ /* Turn MUX off */

				esp_state.state &= ~ESP_MUX;
				sint32 s;
				for (s=1;s<4;s++) /* Only connection 0 is usable without MUX */
					cu_esp_close_socket(s);
				cu_esp_txp_ok();

			}else if (cmd_buf[3+7] == '1'){ /* Turn MUX on, 4 connections max */
					
				esp_state.state |= ESP_MUX;
				cu_esp_txp_ok();
				
			}else{ /* Error MUX not set */
				cu_esp_txp_error();
			}
		}else
			cu_esp_txp_error();
	}else
		cu_esp_txp_error();
}


void cu_esp_at_bad_command(){

	cu_esp_txp((const char*)"this no fun\r\n"); /* It really says that.. */
}


void cu_esp_at_cwqap(sint8 *cmd_buf){

	if (!strncmp((char*)&cmd_buf[8],"?\r\n",3)){ /* Do something special in this case for some firmwares? */

	}else if (!strncmp((char*)&cmd_buf[8],"\r\n",2)){

		cu_esp_close_socket(ESP_ALL_CONNECTIONS);
		esp_state.state &= ~ESP_AP_CONNECTED;
		esp_state.state &= ~ESP_INTERNET_ACCESS;
		cu_esp_txp_ok();
	}else /* Bad format */
		cu_esp_txp_error();
}


void cu_esp_at_gslp(sint8 *cmd_buf){

	if (cmd_buf[8] < '1' || cmd_buf[8] > '9'){

		cu_esp_txp_error();
		return;
	}
	sint32 i;
	for (i=0;i<sizeof(esp_state.cmd_buf);i++){ /* Terminate the string so Atoi works as expected */
		if (cmd_buf[i] == '\r'){
			cmd_buf[i] = 0;
			break;
		}
	}
	sint32 usec = cu_esp_atoi((char *)&esp_state.cmd_buf);
	/* if (usec > ?)usec = ?;  TODO - find out the limits on the real hardware */
	cu_esp_timed_stall(ESP_AT_MS_DELAY*usec);
}


void cu_esp_at_cwlif(sint8 *cmd_buf){ /* https://github.com/espressif/esp_AT/wiki/CWLIF */

	cu_esp_timed_stall(ESP_AT_OK_DELAY);
	cu_esp_txp_ok();
}


void cu_esp_at_cwdhcp(sint8 *cmd_buf){ /* https://github.com/espressif/esp_AT/wiki/CWDHCP we fake this */

	cu_esp_timed_stall(ESP_AT_OK_DELAY);
	cu_esp_txp_ok();
}


uint32 cu_esp_verify_mac_string(uint32 off){ /* Pass the offset to the first '"' at the start of the command string */
/*
	if (cmd_buf[off] != '"' || cmd_buf[off+12+5] != '"')
		return 1;
	
	for (uint32 i=off+1;i<off+12+5;i+=3){
		char c = cmd_buf[i];
		if (c < '0' || (c > '9' && c < 'A') || (c < 'a' && c > 'F') || c > 'F')
			return 0;

		c = cmd_buf[i+1];
		if (c < '0' || (c > '9' && c < 'A') || (c < 'a' && c > 'F') || c > 'F')
			return 0;

		if (i <= off+12+3 && cmd_buf[i+2] != ':')
			return 0;
	}
*/
	return 0;
}


void cu_esp_save_station_mac(sint8 *cmd_buf){

}


void cu_esp_at_cipstamac(sint8 *cmd_buf){ /* https://github.com/espressif/esp_AT/wiki/CIPSTAMAC */

	if (!strncmp((const char*)&cmd_buf[12],"?\r\n",3)){ /* Query only */

		cu_esp_timed_stall(ESP_AT_OK_DELAY);
		cu_esp_txp("+CIPSTAMAC:");
		cu_esp_txp((char *)esp_state.station_mac);
		cu_esp_txp("\r\n");
		cu_esp_txp_ok();

	}else{
		if (!cu_esp_verify_mac_string(13)){
			cu_esp_txp_error();
			return;
		}
		cmd_buf[14+12+5] = '\0'; /* Overwrite last '"' and make it into a proper string */
		strcpy((char *)&cmd_buf[14],(char *)esp_state.station_mac);
		
	}
}


void cu_esp_at_cipapMAC(sint8 *cmd_buf){ /* https://github.com/espressif/esp_AT/wiki/CIPAPMAC */

	if (!strncmp((const char*)&cmd_buf[11],"?\r\n",3)){ /* Query only */

		/* cu_esp_timed_stall(ESP_AT_OK_DELAY); */
		cu_esp_txp("+CIPAPMAC:\"");
		cu_esp_txp((char *)esp_state.soft_ap_mac);
		cu_esp_txp("\"\r\n");
		cu_esp_txp_ok();

	}else{

		if (!cu_esp_verify_mac_string(12)){
			cu_esp_txp_error();
			return;
		}
		cmd_buf[13+12+5] = '\0'; /* Overwrite last " and make it into a proper string */
		strcpy((char *)&cmd_buf[13],(char *)esp_state.soft_ap_mac);
	}
}


void cu_esp_at_cipsta(sint8 *cmd_buf){ /* https://github.com/espressif/esp_AT/wiki/CIPSTA */

	if (!strncmp((const char*)&cmd_buf[9],"?\r\n",3)){ /* Query only */

		cu_esp_timed_stall(ESP_AT_OK_DELAY);
		cu_esp_txp("+CIPSTA:");
		cu_esp_txp((char *)esp_state.station_ip);
		cu_esp_txp("\r\n");
		cu_esp_txp_ok();

	}else{

		if (!cu_esp_verify_mac_string(10)){
			cu_esp_txp_error();
			return;
		}
		cmd_buf[11+12+5] = '\0'; /* Overwrite last " and make it into a proper string */
		strcpy((char *)&cmd_buf[11],(char *)esp_state.station_ip);
	}
}


void cu_esp_at_cipap(sint8 *cmd_buf){ /* https://github.com/espressif/esp_AT/wiki/CIPAP */

	if (!strncmp((const char*)&cmd_buf[8],"?\r\n",3)){ /* Query only */

		/* if (esp_state.debug_level > 0)	print_message("AT+CIPAP query only\n",0); */
		cu_esp_timed_stall(ESP_AT_OK_DELAY);
		cu_esp_txp("+CIPAP:");
		cu_esp_txp((char *)esp_state.soft_ap_ip);
		cu_esp_txp("\r\n");
		cu_esp_txp_ok();
	}else{

		if (!cu_esp_verify_mac_string(10)){
			cu_esp_txp_error();
			return;
		}
		cmd_buf[10+12+5] = '\0'; /* Overwrite last '"' and make it into a proper string */
		strcpy((char *)&cmd_buf[10],(char *)esp_state.soft_ap_ip);
	}
}


void cu_esp_at_cipsto(sint8 *cmd_buf){ /* https://github.com/espressif/esp_AT/wiki/CIPSTO */
	/* Server timeout, range 0~7200 seconds */
	/* If 0, will never timeout */

	if (!strncmp((const char*)&cmd_buf[9],"?\r\n",3)){ /* Query only */

		cu_esp_txp("+CIPSTO:");
		cu_esp_txi(esp_state.server_timeout);
		cu_esp_txp("\r\n");
	}else{
		uint32 val = cu_esp_atoi((char *)&cmd_buf[10]);
		if (val > 7200)
			cu_esp_txp_error();
		else{
			esp_state.server_timeout = val;
			cu_esp_txp_ok();
		}
	}
}


void cu_esp_at_cifsr(sint8 *cmd_buf){ /* https://github.com/espressif/esp_AT/wiki/CIFSR */

	/* TODO DO SOFTAP, OR NO IP IF NOT CONNECTED */
	cu_esp_txp("+CIFSR:");
	cu_esp_txp((char *)esp_state.wifi_ip);
	cu_esp_txp_ok();
}


void cu_esp_at_uart(sint8 *cmd_buf){ /* also "AT+UART_CUR" and "AT+UART_DEF" */
cu_esp_txp_ok(); //HACK
return;
//	uint32 b = Atoi((char *)&cmd_buf[8]);
	uint32 i=0;
	for (i=0;i<10;i++){
		if (cmd_buf[8+i] < '0' || cmd_buf[8+i] > '9')
			break;
	}
	if (i == 10 || cmd_buf[8+i] != ','){
		cu_esp_txp_error();
		return;
	}
}


void cu_esp_at_rfpower(sint8 *cmd_buf){
	/* Range 0 ~ 82, unit:0.25dBm */
	if (0 && !strncmp((char*)&cmd_buf[3+7],"?\r\n",3)){ /* query only...not supported on real hardware? */
		cu_esp_txp("+RFPOWER:\"");
		cu_esp_txi(esp_state.rf_power);
		cu_esp_txp("\"\r\n");

	}else{ /* Set */
		char c1 = cmd_buf[3+8];
		char c2 = cmd_buf[3+9];
		char c3 = cmd_buf[3+10];

		if (cmd_buf[3+7] != '=' || c1 < '0' || c1 > '9' || (c2 != '\r' && (c2 < '0' || c2 > '9')) || (c2 == '\r' && c3 != '\n')){ 
			cu_esp_txp_error();
			return;
		}

		int p = cmd_buf[3+8] - '0';
		if (cmd_buf[3+9] != '\r')
			p += cmd_buf[3+9]-'0';
		
		if (p > 82){ /* out of range */
			cu_esp_txp_error();
			return;
		}

		esp_state.rf_power = p;
	}
	
	cu_esp_txp_ok();
}


void cu_esp_at_rfvdd(sint8 *cmd_buf){


}


void cu_esp_reset_factory(){

	esp_state.uart_baud_bits_module_default = ESP_FACTORY_DEFAULT_BAUD_BITS; /* 9600 baud */
	esp_state.uart_baud_bits_module = ESP_FACTORY_DEFAULT_BAUD_BITS;
	esp_state.baud_rate=9600UL;

	memset(esp_state.soft_ap_name,'\0',sizeof(esp_state.soft_ap_name));
	sprintf((char *)esp_state.soft_ap_name,"CUzeBox SoftAP");
		
	memset(esp_state.soft_ap_pass,'\0',sizeof(esp_state.soft_ap_pass));
	sprintf((char *)esp_state.soft_ap_pass,"password");
		
	memset(esp_state.soft_ap_mac,'\0',sizeof(esp_state.soft_ap_mac));
	sprintf((char *)esp_state.soft_ap_mac,"ec:44:4a:67:cb:d8");
		
	memset(esp_state.soft_ap_ip,'\0',sizeof(esp_state.soft_ap_ip));
	sprintf((char *)esp_state.soft_ap_ip,"10.0.0.1");

	memset(esp_state.wifi_name,'\0',sizeof(esp_state.wifi_name));
	sprintf((char *)esp_state.wifi_name,"uzenet");

	memset(esp_state.wifi_pass,'\0',sizeof(esp_state.wifi_pass));
	sprintf((char *)esp_state.wifi_pass,"h6dkj90xghrwx89hncx59ktre61hb2k77de67v1");

	memset(esp_state.wifi_mac,'\0',sizeof(esp_state.wifi_mac));
	sprintf((char *)esp_state.wifi_mac,"60:22:32:e5:f3:85");

	memset(esp_state.wifi_ip,'\0',sizeof(esp_state.wifi_ip));
	sprintf((char *)esp_state.wifi_ip,"10.0.0.1");

}


void cu_esp_at_restore(sint8 *cmd_buf){
	
	cu_esp_reset_factory();
	cu_esp_save_config();
	cu_esp_txp_ok();
	cu_esp_at_rst();
}


void cu_esp_at_cipdinfo(sint8 *cmd_buf){

	if (0 && !strncmp((char*)&cmd_buf[3+8],"?\r\n",3)){ /* Query only...not supported on real hardware? */
		cu_esp_txp("+CIPDINFO:");
		cu_esp_txi((esp_state.state & ESP_CIPDINFO)?1:0);
		cu_esp_txp("\r\n");
	}else if (cmd_buf[3+8] == '=' && (cmd_buf[3+9] == '0' || cmd_buf[3+9] == '1') && cmd_buf[3+10] == '\r' && cmd_buf[3+11] == '\n'){
		
		if (cmd_buf[3+9] == '0')
			esp_state.state &= ~ESP_CIPDINFO;
		else
			esp_state.state |= ESP_CIPDINFO;
		cu_esp_txp_ok();
	}else
		cu_esp_txp_error();

}

void cu_esp_at_ping(sint8 *cmd_buf){

/*
	for(loop=0;loop < 10; loop++){
		int len=sizeof(r_addr);
		if ( recvfrom(sd, &pckt, sizeof(pckt), 0, (struct sockaddr*)&r_addr, &len) > 0 ){
		return;
	}
	bzero(&pckt, sizeof(pckt));
	pckt.hdr.type = ICMP_ECHO;
	pckt.hdr.un.echo.id = pid;
	for ( i = 0; i < sizeof(pckt.msg)-1; i++ )
		pckt.msg[i] = i+'0';
	pckt.msg[i] = 0;
	pckt.hdr.un.echo.sequence = cnt++;
	pckt.hdr.checksum = checksum(&pckt, sizeof(pckt));
	if ( sendto(sd, &pckt, sizeof(pckt), 0, (struct sockaddr*)addr, sizeof(*addr)) <= 0 )
		print_message("ESP ERROR: ping sendto() failed: %d\n", cu_esp_get_last_error());
		usleep(300000);
	}
	return 1;
*/
}


void cu_esp_at_cwautoconn(sint8 *cmd_buf){

	if (0 && !strncmp((char*)&cmd_buf[3+10],"?\r\n",3)){ /* Query only...not supported on real hardware? */
		cu_esp_txp("+CWAUTOCONN:");
		cu_esp_txi((esp_state.state & ESP_AUTOCONNECT)?1:0);
		cu_esp_txp("\r\n");
	}else if (cmd_buf[3+10] == '=' && (cmd_buf[3+11] == '0' || cmd_buf[3+11] == '1') && cmd_buf[3+12] == '\r' && cmd_buf[3+13] == '\n'){
		if (cmd_buf[3+11] == '0')
			esp_state.state &= ~ESP_AUTOCONNECT;
		else
			esp_state.state |= ESP_AUTOCONNECT;
		cu_esp_txp_ok();
	}else
		cu_esp_txp_error();
}


void cu_esp_at_save_translink(sint8 *cmd_buf){

	sint8 host[160];
	sint32 i,j;
	for (i=20;i<20+128;i++){
		if (cmd_buf[i] == '"')
			break;
		host[i-20] = cmd_buf[i];
		host[(i-20)+1] = '\0';
	}

	char c1 = cmd_buf[17]; /* 1 or 0, start unvarnished or not */
	char c2 = cmd_buf[i];
	char c3 = cmd_buf[i+1];

	/* "AT+SAVETRANSLINK=1,"uzebox.net",5800,"TCP"\r\n */

	int port = cu_esp_atoi((char *)&cmd_buf[i+3]); /* Get the port number */
	
	if (c2 != '"' || c3 != ',' || (c1 != '0' && c1 != '1') || cmd_buf[18] != ',' || cmd_buf[19] != '"' || port < 1 || port > 65535){
		cu_esp_txp_error();
		return;
	}

	for (j=i+3;j<i+3+6;j++){ /* Make sure the port number is followed by ," */
		if (cmd_buf[j] == ',' && cmd_buf[j+1] == '"')
			break;
	}
	
	if (j == i+3+6 || cmd_buf[j+5] != '"' || cmd_buf[j+6] != '\r' || cmd_buf[j+7] != '\n'){
		cu_esp_txp_error();
		return;
	}

	sint32 proto = 0;

	if (!strncmp((const char *)&cmd_buf[j+2],"TCP",3)){
		proto = 0;		
	}else if (!strncmp((const char *)&cmd_buf[j+2],"UDP",3)){
		proto = 1;
	}else{
		cu_esp_txp_error();
		return;
	}

	esp_state.translink_proto = (proto == 0)?ESP_PROTO_TCP:ESP_PROTO_UDP;
	strcpy((char *)esp_state.translink_host,(char *)host);
	esp_state.translink_port = port;
	/* TODO SAVE TO DISK */		
	cu_esp_txp_ok();
}

void cu_esp_at_cipbufreset(sint8 *cmd_buf){
	/* Fails if not all segments are sent yet or no connection */		
	sint32 connection = 255;

	if (!(esp_state.state & ESP_MUX)){ /* Single mode */
		if (!strncmp((const char *)&cmd_buf[14],"\r\n",2))
			connection = 0;

	}else{ /* Multiple connection mode */
		if (!strncmp((const char *)&cmd_buf[16],"\r\n",2)){
			if (cmd_buf[15] > '0' && cmd_buf[13] < '9')
				connection = (cmd_buf[13]-'0')+1;
		}
	}
	
	if (connection == 255)
		cu_esp_txp_error();
	else{
		/* TCPSegID[connection] = 0; */
		cu_esp_txp_ok();
	}
}

void cu_esp_at_cipcheckseq(sint8 *cmd_buf){
	cu_esp_at_bad_command();
}

void cu_esp_at_cipbufstatus(sint8 *cmd_buf){
	cu_esp_at_bad_command();
}

void cu_esp_at_cipsendbuf(sint8 *cmd_buf){
	/* TCPSendBuf[] = ... */
}

void cu_esp_at_sendex(sint8 *cmd_buf){
	cu_esp_at_bad_command();
}


void cu_esp_at_cwstartsmart(sint8 *cmd_buf){
	esp_state.state |= ~ESP_SMARTCONFIG_ACTIVE;
}


void cu_esp_at_cwstopsmart(sint8 *cmd_buf){
	esp_state.state &= ~ESP_SMARTCONFIG_ACTIVE;
}


void cu_esp_at_ciupdate(sint8 *cmd_buf){ /* https://github.com/espressif/esp_AT/wiki/CIUPDATE */

	cu_esp_timed_stall(ESP_UZEBOX_CORE_FREQUENCY/4);
	cu_esp_txp("1: found server\n");
	cu_esp_timed_stall(ESP_UZEBOX_CORE_FREQUENCY/10);
	cu_esp_txp("2: connect server\n");
	cu_esp_timed_stall(ESP_UZEBOX_CORE_FREQUENCY/8);
	cu_esp_txp("3: got edition\n");
	cu_esp_timed_stall(ESP_UZEBOX_CORE_FREQUENCY/10);
	cu_esp_txp("4: start update\n");
	cu_esp_timed_stall(ESP_UZEBOX_CORE_FREQUENCY*5); /* Fake writing new version to flash... */
	cu_esp_at_rst();
}


void cu_esp_at_rst(sint8 *cmd_buf){

	esp_state.state &= ~ESP_DID_FIRST_TICK; /* Module will reset as soon as the main tick iterates */
	esp_state.wifi_timer = 1; /* Now the module will take a bit to reconnect to the Wifi AP */
	/* esp_state.wifi_delay = ESP_at_cwjap_DELAY;//+((rand()%1000)*ESP_AT_MS_DELAY); */
}


void cu_esp_at_gmr(){

	//cu_esp_timed_stall(ESP_AT_OK_DELAY);
	cu_esp_txp(at_gmr_string);
}


auint cu_esp_set_baud_divisor(auint new_divisor){ /* http://wormfood.net/avrbaudcalc.php?bitrate=&clock=28.63636&databits=8 */
	switch(new_divisor){
		case 4800: esp_state.baud_divisor = 372; break;
		case 9600: esp_state.baud_divisor = 185; break;
		case 14400: esp_state.baud_divisor = 124; break;
		case 19200: esp_state.baud_divisor = 92; break;
		case 28800: esp_state.baud_divisor = 61; break;
		case 38400: esp_state.baud_divisor = 46; break;
		case 57600: esp_state.baud_divisor = 30; break;
		case 76800: esp_state.baud_divisor = 22; break;
		case 115200: esp_state.baud_divisor = 15; break;
		case 230400: esp_state.baud_divisor = 7; break;
		default: return 1; break;
	};
	return 0;
}



void cu_esp_at_ciobaud(sint8 *cmd_buf){

	uint32 new_divisor = atoi((char*)&cmd_buf[11]);

	cu_esp_timed_stall(ESP_AT_OK_DELAY);
	if(cu_esp_set_baud_divisor(new_divisor))
		cu_esp_txp_error();
	else
		cu_esp_txp_ok();
}



void cu_esp_process_at(sint8 *cmd_buf){ /* this is only called after it has been determined there is something ending with \r\n, and has been string terminated */
//return;
print_message("PROCESS AT [%s]\n", cmd_buf);

	if (strncmp((char*)cmd_buf, "AT+", 3)){ /* No way an AT+X command has been formed */

		if(cmd_buf[0] != 'A' || cmd_buf[1] != 'T'){ /* command is terminated, but doesn't start with AT */
			cu_esp_txp_error();
			return;
		}
		if (!strncmp((char*)cmd_buf,"ATE",3)){ /* Check special case for echo on/off */

			cu_esp_at_ate(cmd_buf);
			return;

		}else if (!strncmp((char*)cmd_buf,"AT\r\n",4)){ /* Check special case for AT */

			cu_esp_at();
			return;

		}
		cu_esp_txp_error();//something else?
		return;
	}

	/* To get here we know the buffer starts with "AT+" */

	if (cmd_buf[3] == 'C'){ /* "C.." */
		
		if (cmd_buf[4] == 'I'){ /* "CI.." */

			/* if (commandlen == ... */
			
			if (!strncmp((char*)&cmd_buf[5],"PSEND",5)) /* send a packet(most common command during gameplay) */
				cu_esp_at_cipsend(cmd_buf);
			else if (!strncmp((char*)&cmd_buf[5],"PSTART=",7)) /* starting a connection */
				cu_esp_at_cipstart(cmd_buf);
			else if (!strncmp((char*)&cmd_buf[5],"PCLOSE",6)) /* Close a connection, different format depending on CIPMUX? */
				cu_esp_at_cipclose(cmd_buf);
			else if (!strncmp((char*)&cmd_buf[5],"PMODE=",6)) /* Set packet sending mode */
				cu_esp_at_cipmode(cmd_buf);
			else if (!strncmp((char*)&cmd_buf[5],"FSR",3)) /* List ip addresse(s) */
				cu_esp_at_cifsr(cmd_buf);
			else if (!strncmp((char*)&cmd_buf[5],"PSTATUS",7)) /* Get the status of connections */
				cu_esp_at_cipstatus(cmd_buf);
			else if (!strncmp((char*)&cmd_buf[5],"PSERVER=",8)) /* Turn listen mode on or off */
				cu_esp_at_cipserver(cmd_buf);
			else if (!strncmp((char*)&cmd_buf[5],"PMUX",4)) /* Turn multiple connections mode on or off */
				cu_esp_at_cipmux(cmd_buf);
			else if (!strncmp((char*)&cmd_buf[5],"OBAUD=",6)) /* Change the baud rate(certain firmwares) */
				cu_esp_at_ciobaud(cmd_buf);
			else if (!strncmp((char*)&cmd_buf[5],"PSTAMAC",7)) /* Set station mac address */
				cu_esp_at_cipstamac(cmd_buf);
			else if (!strncmp((char*)&cmd_buf[5],"PSTA",4)) /* Set ip address of station */
				cu_esp_at_cipsta(cmd_buf);
			else if (!strncmp((char*)&cmd_buf[5],"PAPMAC",6))
				cu_esp_at_cipapMAC(cmd_buf+6);
			else if (!strncmp((char*)&cmd_buf[5],"PAP",3)) /* Set ip address of softAP(access point emulated by 8266) */
				cu_esp_at_cipap(cmd_buf);
			else if (!strncmp((char*)&cmd_buf[5],"PSTO",4)) /* Set server time out(0~7200 seconds) */
				cu_esp_at_cipsto(cmd_buf);
			else if (!strncmp((char*)&cmd_buf[5],"UPDATE",6)) /* Get firmware upgrade OTA(fake) */
				cu_esp_at_ciupdate(cmd_buf);
			else
				cu_esp_at_bad_command();
		
		}else if (cmd_buf[4] == 'W'){ /* "CW.." */

			if (!strncmp((char*)&cmd_buf[5],"QAP",3)) /* quit/disconnect from the AP */
				cu_esp_at_cwqap(cmd_buf);
			else if (!strncmp((char*)&cmd_buf[5],"LAP\r\n",5)) /* List APs, fake, could be real */
				cu_esp_at_cwlap(cmd_buf);
			else if (!strncmp((char*)&cmd_buf[5],"SAP",3)) /* Set wifi logon credentials for the HOSTED ap */
				cu_esp_at_cwsap(cmd_buf);
			else if (!strncmp((char*)&cmd_buf[5],"JAP",3)) /* This will always pass */
				cu_esp_at_cwjap(cmd_buf);
			else if (!strncmp((char*)&cmd_buf[5],"MODE",4)) /* We are not emulating AP mode, but our OS is */
				cu_esp_at_cwmode(cmd_buf);
			else
				cu_esp_at_bad_command();

		}
	
	}else{ /* !"C.." */

		if (!strncmp((char*)&cmd_buf[3],"GMR\r\n",5)) /* Get firmware version, no arguments possible */
			cu_esp_at_gmr();
		else if (!strncmp((char*)&cmd_buf[3],"IPR=",4)) /* Change baud rate(depreciated) */
			cu_esp_at_ipr(cmd_buf);
		else if (!strncmp((char*)&cmd_buf[3],"UART",4)) /* Change baud rate(depreciated?) */
			cu_esp_at_uart(cmd_buf);
		else if (!strncmp((char*)&cmd_buf[3],"GSLP=",5)) /* Go to deep sleep(milliseconds) */
			cu_esp_at_gslp(cmd_buf);
		else if (!strncmp((char*)&cmd_buf[3],"RST\r\n",5)) /* Reset module, no arguments allowed */
			cu_esp_at_rst(cmd_buf);
		else{ /* If we get here, we did receive an "AT+" and the ending "\r\n", but the command doesn't fit anything */
			cu_esp_at_bad_command(); /* literally says:"this no fun.." */
		}
	}
}


sint32 cu_esp_process_ipd(){ /* Get network data, put it to Tx(max 256 bytes at a time) */
//print_message("IPD\n");
	sint32 i,num_bytes;
	num_bytes = 0;
	for(i=0;i<4;i++){

		if(esp_state.socks[i] == ESP_INVALID_SOCKET) /* MUX is automatically handled by this mechanism */
			continue;

		if(esp_state.user_input_mode == ESP_USER_MODE_UNVARNISHED && i > 0) /* transparent transmission only works on the first socket */
			break;

		/* Recv() automatically handles UDP or TCP...TODO */

		/* since we are using non-blocking sockets, they frequently return an "error" indicating they "would have blocked" for new data */
esp_state.rx_packet[0] = '\0';
		num_bytes = cu_esp_net_recv(i,esp_state.rx_packet,sizeof(esp_state.rx_packet),0);
		if(num_bytes == ESP_SOCKET_ERROR){ /* No data is available or socket error */
			auint last_error = cu_esp_get_last_error();
			if (last_error != ESP_WOULD_BLOCK && last_error != ESP_EAGAIN){ /* actual error, disconnected? */
				print_message("ESP Socket Error, terminating connection %d: %d\n", i, cu_esp_get_last_error()); 
				if (esp_state.state & ESP_MUX){
					cu_esp_txp((const char*)"CLOSED ");
					cu_esp_txi(i); /* Connection number */
					cu_esp_txp((const char*)"\r\n");
				}else
					cu_esp_txp((const char*)"CLOSED\r\n");
				cu_esp_close_socket(i);
				return 0; /* not sure, there might be a delay here before more data is sent? would this send if unvarnished?? */
			}
			continue; /* otherwise not an error, there just wasn't any data yet */
		}else if(num_bytes == 0){
			//print_message("RECEIVED 0 BYTES\n");
			continue;
		}

		esp_state.rx_packet[num_bytes] = '\0';
//print_message("RECEIVED SOME DATA[%s] %d bytes\n", esp_state.rx_packet, num_bytes);
		if(esp_state.user_input_mode == ESP_USER_MODE_UNVARNISHED){
	//		print_message("Got unvarnished:[%s]\n",esp_state.rx_packet);
			cu_esp_txl((const char *)esp_state.rx_packet, num_bytes);
		}
			
	} /* for */


	/* If server mode is active, listen for an incoming connection */

//	if (esp_state.server_timer){

//		i = accept(esp_state.socks[4], 128);
//		if (i == -1){ /* Some error, most likely nothing is trying to connect on a non-blocking socket */
//			if (cu_esp_get_last_error() != ESP_WOULD_BLOCK){ /* It is an actual error? */
//				print_message("ESP ERROR: accept() failed, stopping server mode: %d\n", cu_esp_get_last_error());
//				cu_esp_close_socket(4);
//			}
//		}

//	}

	return num_bytes;
}
/********************************/
/*                              */
/*         Flash/File           */
/*                              */
/********************************/

void cu_esp_save_config(){

	esp_state.flash_dirty = 0;

	FILE *f = fopen("esp.cfg","w");
	if (f == NULL){ /* we always load at least once before a save, so this should have been created... */
		print_message("ESP ERROR: Can't open esp.cfg to save settings\n");
		return;
	}

	fprintf(f, "SoftApName=\"%s\"\n", esp_state.soft_ap_name);
	fprintf(f, "SoftApPass=\"%s\"\n", esp_state.soft_ap_pass);
	fprintf(f, "SoftApMac=\"%s\"\n", esp_state.soft_ap_mac);
	fprintf(f, "SoftApIp=\"%s\"\n", esp_state.soft_ap_ip);
	fprintf(f, "WifiName=\"%s\"\n", esp_state.wifi_name);
	fprintf(f, "WifiPass=\"%s\"\n", esp_state.wifi_pass);
	fprintf(f, "WifiMac=\"%s\"\n", esp_state.wifi_mac);
	fprintf(f, "WifiIp=\"%s\"\n", esp_state.wifi_ip);
	fprintf(f, "Baud=\"%d\"\n", esp_state.baud_rate);
	fclose(f);
	print_message("ESP: Saved Config esp.cfg\n");
}


auint cu_esp_load_config(){

	esp_state.flash_dirty = 0;
	auint ret = 0;

	FILE *f = fopen("esp.cfg","r");
	if (f == NULL){ /* try creating a fresh config */
		print_message("\nESP: esp.cfg settings file does not exist, using factory defaults\n");
		cu_esp_reset_factory();
		cu_esp_save_config();
		return 0;
	}

	char buf[512];
	while(fgets(buf, sizeof(buf), f)){ /* parse each line */
		if(buf[strspn(buf, " ")] == '\n') /* accept blank lines */
			continue;
		if(buf[0] == '#') /* accept comment lines */
			continue;

		if(sscanf(buf, " SoftApName = \"%[^\"]\" ", esp_state.soft_ap_name)){
			continue;
		}else if(sscanf(buf, " SoftApPass = \"%[^\"]\" ", esp_state.soft_ap_pass)){
			continue;
		}else if(sscanf(buf, " SoftApMac = \"%[^\"]\" ", esp_state.soft_ap_mac)){
			continue;
		}else if(sscanf(buf, " SoftApIp = \"%[^\"]\" ", esp_state.soft_ap_ip)){
			continue;
		}else if(sscanf(buf, " WifiName = \"%[^\"]\" ", esp_state.wifi_name)){
			continue;
		}else if(sscanf(buf, " WifiPass = \"%[^\"]\" ", esp_state.wifi_pass)){
			continue;
		}else if(sscanf(buf, " WifiMac = \"%[^\"]\" ", esp_state.wifi_mac)){
			continue;
		}else if(sscanf(buf, " WifiIp = \"%[^\"]\" ", esp_state.wifi_ip)){
			continue;
		}else if(sscanf(buf, " Baud = \"%u\" ", &esp_state.baud_rate)){
			continue;
		}
	}

	print_message("ESP: Loaded CFG File:\n");
	print_message("\tWifiName[%s]\n", esp_state.wifi_name);
	print_message("\tWifiPass[%s]\n", esp_state.wifi_pass);
	print_message("\tWifiMac[%s]\n", esp_state.wifi_mac);
	print_message("\tWifiIp[%s]\n", esp_state.wifi_ip);
	print_message("\tSoftApName[%s]\n", esp_state.soft_ap_name);
	print_message("\tSoftApPass[%s]\n", esp_state.soft_ap_pass);
	print_message("\tSoftApMac[%s]\n", esp_state.soft_ap_mac);
	print_message("\tSoftApIp[%s]\n", esp_state.soft_ap_ip);
	print_message("\tBaud[%d]\n", esp_state.baud_rate);
	fclose(f);
	
	return ret;
}


/********************************/
/*                              */
/*       Network/Sockets        */
/*                              */
/********************************/
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
//HACK, this function is only available if #define WINVER 0x6000 or greater?!
//just throw it in for now...
int inet_pton(int af, const char *src, void *dst)
{
  struct sockaddr_storage ss;
  int size = sizeof(ss);
  char src_copy[INET6_ADDRSTRLEN+1];

  ZeroMemory(&ss, sizeof(ss));
  /* stupid non-const API */
  strncpy (src_copy, src, INET6_ADDRSTRLEN+1);
  src_copy[INET6_ADDRSTRLEN] = 0;

  if (WSAStringToAddress(src_copy, af, NULL, (struct sockaddr *)&ss, &size) == 0) {
    switch(af) {
      case AF_INET:
    *(struct in_addr *)dst = ((struct sockaddr_in *)&ss)->sin_addr;
    return 1;
      case AF_INET6:
    *(struct in6_addr *)dst = ((struct sockaddr_in6 *)&ss)->sin6_addr;
    return 1;
    }
  }
  return 0;
} 
#endif
sint32 cu_esp_net_connect(sint8 *hostname, uint32 sock, uint32 port, uint32 type){

print_message("STARTING CONNECTION TO [%s], conn: %d, port: %d, type: %s\n", hostname, sock, port, (type == ESP_PROTO_TCP ? "TCP":"UDP"));

#ifdef __EMSCRIPTEN__
	if(!esp_state.ws_proxy_enabled){
		bridgeSocket = emscripten_init_websocket_to_posix_socket_bridge("ws://localhost:8080");
		// Synchronously wait until connection has been established.
		uint16 readyState = 0;
		do {
			emscripten_websocket_get_ready_state(bridgeSocket, &readyState);
			emscripten_thread_sleep(100);
		} while (readyState == 0);
		esp_state.ws_proxy_enabled = 1;
	}
#endif

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	LPHOSTENT hostEntry;
	hostEntry = gethostbyname(hostname);
	if(!hostEntry){
		print_message("ESP: gethostbyname() failed: %d\n", cu_esp_get_last_error());
		return INVALID_SOCKET;
	}

	esp_state.socks[sock] = socket(AF_INET,(type & ESP_PROTO_TCP)?SOCK_STREAM:SOCK_DGRAM,(type & ESP_PROTO_TCP)?IPPROTO_TCP:0);

	if(esp_state.socks[sock] == INVALID_SOCKET){
		print_message("ESP: Failed to create socket: %d\n", cu_esp_get_last_error());
		return INVALID_SOCKET;
	}

	esp_state.sock_info[sock].sin_family = AF_INET;
	esp_state.sock_info[sock].sin_addr = *((LPIN_ADDR)*hostEntry->h_addr_list);
	esp_state.sock_info[sock].sin_port = htons(port);

	if((connect(esp_state.socks[sock],(LPSOCKADDR)&esp_state.sock_info[sock],sizeof(struct sockaddr))) == SOCKET_ERROR){
		esp_state.socks[sock] = INVALID_SOCKET;

		if(cu_esp_get_last_error() == WSAECONNREFUSED){
			print_message("ESP: Remote actively refused connection, wrong port?\n");
			return INVALID_SOCKET;
		}else{
			print_message("ESP: Failed to connect, timeout or socket error: %d\n",cu_esp_get_last_error());
			return INVALID_SOCKET;
		}

		unsigned long block_mode = 1;
		if(ioctlsocket(esp_state.socks[sock],FIONBIO,&block_mode) != NO_ERROR){
			print_message("ESP: ioctlsocket() failed to set non-blocking mode: %d\n",cu_esp_get_last_error());
//HACK disable for Emscripten			return INVALID_SOCKET;
		}
	}
#else


	/* do things the modern non-depreciated way, now supporting IPv6! */
	char hostport[8];
	sprintf(hostport, "%d", port);
	struct in6_addr host_addr;
	struct addrinfo ahints;
	struct addrinfo *ares = NULL;

	memset(&ahints, 0x00, sizeof(ahints));

	ahints.ai_flags    = AI_NUMERICSERV;

	ahints.ai_family   = AF_UNSPEC;
	ahints.ai_socktype = SOCK_STREAM;


	sint32 r = inet_pton(AF_INET, (char *)hostname, &host_addr);


	if(r == 1){ /* IPv4 address? */
		ahints.ai_family = AF_INET;
		ahints.ai_flags |= AI_NUMERICHOST;
	}else{ /* prefer IPv6 */
		r = inet_pton(AF_INET6, (char *)hostname, &host_addr);
		if(r == 1){ /* IPv6 address? */
       
			ahints.ai_family = AF_INET6;
			ahints.ai_flags |= AI_NUMERICHOST;
		}
	}

	sint32 rc = getaddrinfo((char *)hostname, hostport, &ahints, &ares);  /* get a linked list of addresses for the hostname */

	if(rc != 0){

		print_message("ESP ERROR: can't find host [%s]: %d\n", (char *)hostname, (int)cu_esp_get_last_error());
		if(rc != EAI_AGAIN)
			print_message("ESP ERROR: getaddrinfo() failed: %d\n", (int)cu_esp_get_last_error());
		return -1;
	}

	do{ /* try each entry in the linked list, hopefully we can connect to one of them... */

		if(esp_state.socks[sock] != ESP_INVALID_SOCKET) /* for some reason need to create a new socket each attempt? */
			close(esp_state.socks[sock]);

		esp_state.socks[sock] = socket(ares->ai_family, ares->ai_socktype, ares->ai_protocol);
		if(esp_state.socks[sock] < 0){
			print_message("ESP ERROR: socket() failed: %d\n", cu_esp_get_last_error());
			return -1;
		}

		rc = connect(esp_state.socks[sock], ares->ai_addr, ares->ai_addrlen);
		if(rc < 0)
			ares = ares->ai_next;
		else
			break;
	}while(ares != NULL);


//TODO THIS SEEMS TO BE THE WAY TO DO IT IN EMSCRIPTEN???!?
//fcntl(_data->socket, F_SETFL, O_NONBLOCK);
//https://blog.squareys.de/emscripten-sockets/



#endif
	auint block_mode = 1;
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	if (ioctlsocket(esp_state.socks[sock],FIONBIO,(u_long *)&block_mode) != 0){ print_message("ESP ERROR: failed to set non-blocking mode: %d\n", cu_esp_get_last_error()); /*return ESP_INVALID_SOCKET; */ } //HACK DISABLE RETURN FOR EMSCRIPTEN
#else
	if (ioctl(esp_state.socks[sock],FIONBIO,&block_mode) != 0){ print_message("ESP ERROR: failed to set non-blocking mode: %d\n", cu_esp_get_last_error()); /*return ESP_INVALID_SOCKET; */  }

//	sint8 optval = 1;
//	int optlen = sizeof(sint8);
	/* disable Nagles Algorithm */
//	if(setsockopt(esp_state.socks[sock],IPPROTO_TCP,TCP_NODELAY,(char *)&optval,optlen) != 0){ print_message("ESP ERROR: failed to set socket option TCP_NODELAY\n"); return ESP_INVALID_SOCKET; }
#endif
	print_message("ESP Connected to [%s] on connection: %d\n", hostname, sock);
	return 0;
}


sint32 cu_esp_listen(uint32 port){

	return 0;

}


sint32 cu_esp_create_socket(uint32 s, uint32 proto, uint32 port){

	if (proto == ESP_PROTO_TCP)
		esp_state.socks[s] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	else /* UDP */
		esp_state.socks[s] = socket(AF_INET, SOCK_DGRAM, 0);

	return esp_state.socks[s];
}


void cu_esp_close_socket(uint32 sock){

	if (sock != ESP_ALL_CONNECTIONS){

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	closesocket(esp_state.socks[sock]);
#else
	close(esp_state.socks[sock]);
#endif
	esp_state.socks[sock] = ESP_INVALID_SOCKET;

	}else{ /* Close all connections */

		for (uint32 i=0;i<4;i++){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
			closesocket(esp_state.socks[i]);
#else
			close(esp_state.socks[i]);
#endif
			esp_state.socks[i] = ESP_INVALID_SOCKET;

		}
	}
}

sint32 cu_esp_get_last_error(){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	return WSAGetLastError();
#else
	return errno;
#endif
}



sint32 cu_esp_net_send(uint32 sock, sint8 *buf, sint32 len, sint32 flags){
//buf[len] = '\0';

//print_message("SEND() sock: %d, buf[%s], len: %d, flags: %d\n", sock, buf, len, flags);

	return send(esp_state.socks[sock], buf, len, flags);
}



sint32  cu_esp_net_recv(uint32 sock, sint8 *buf, sint32 len, sint32 flags){
/* TODO HANDLE UDP */

	return recv(esp_state.socks[sock], buf, sizeof(esp_state.rx_packet), 0);
}


void cu_esp_net_send_unvarnished(sint8 *buf, auint len){
//buf[len] = '\0';
//print_message("SEND UNVARNISHED[%s]\n", buf);
	if(cu_esp_net_send(0, buf, len, 0) == ESP_SOCKET_ERROR){
		print_message("ESP ERROR cu_esp_net_send() failed: %d\n", cu_esp_get_last_error());
	}

}










void cu_esp_timed_stall(auint cycles){ /* TODO make this work the new way...*/
	
	uint8 i;
	for(i=0; i<sizeof(esp_state.delay_pos); i++){ /* find open delay slot */
		if(!esp_state.delay_len[i])
			break;
	}

	if(i == sizeof(esp_state.delay_pos)){ /* no open slots for delays? shouldn't happen, not much can be done... */
		cu_esp_txp_error();
		cu_esp_at_bad_command();
	}

	esp_state.delay_len[i] = cycles;
	esp_state.delay_pos[i] = esp_state.read_buf_pos_in; /* Uzebox can still read previously buffered data before this point in time */
}



void cu_esp_reset_uart(){

	if(esp_state.uart_baud_bits_module_default == 0){ /* failed to load config file? use a factory default */
		esp_state.uart_baud_bits_module_default = ESP_FACTORY_DEFAULT_BAUD_BITS;
		esp_state.baud_rate = ESP_FACTORY_BAUD_RATE;
	}

	esp_state.uart_baud_bits_module = esp_state.uart_baud_bits_module_default;

	esp_state.read_buf_pos_in = esp_state.read_buf_pos_out = 0UL;

	esp_state.state |= ESP_ECHO;
}


void cu_esp_txp(const char *s){ /* Write const string to tx buf */

	auint slen = strlen(s);
	if((esp_state.read_buf_pos_in < esp_state.read_buf_pos_out) && ((esp_state.read_buf_pos_in + slen) >= esp_state.read_buf_pos_out)){ /* this string will fill the buffer? */
		esp_state.read_buf_pos_in = esp_state.read_buf_pos_out = 0UL; /* real device is unpredictable is you spam it, we just trash the buffer and send error instead of trying to model that inner state... */
		cu_esp_txp_error();
		return;
	}
	if((esp_state.read_buf_pos_in + slen) >= sizeof(esp_state.read_buf)){ /* have to split across the end/beginning of the circular buffer? */
		strncpy((char *)esp_state.read_buf+esp_state.read_buf_pos_in, s, (sizeof(esp_state.read_buf)-(esp_state.read_buf_pos_in+1)));
		slen -= (sizeof(esp_state.read_buf)-(esp_state.read_buf_pos_in+1));
		esp_state.read_buf_pos_in = 0;
		cu_esp_txp(s+slen);//recursion to handle the remaining string(to recheck if we will overfill the buffer)
		return;
	}
	strcpy((char *)esp_state.read_buf+esp_state.read_buf_pos_in, s);
	esp_state.read_buf_pos_in += slen;
}

void cu_esp_txl(const char *s, auint len){ /* write fixed length to tx buf */

	if((esp_state.read_buf_pos_in < esp_state.read_buf_pos_out) && ((esp_state.read_buf_pos_in + len) >= esp_state.read_buf_pos_out)){ /* this data will fill the buffer? */
		print_message("<<<<BUFFER OVERFLOW>>>>\n");
		esp_state.read_buf_pos_in = esp_state.read_buf_pos_out = 0UL; /* real device is unpredictable is you spam it, we just trash the buffer and send error instead of trying to model that inner state... */
		cu_esp_txp_error();
		return;
	}
	if((esp_state.read_buf_pos_in + len) >= sizeof(esp_state.read_buf)){ /* have to split across the end/beginning of the circular buffer? */
		strncpy((char *)esp_state.read_buf+esp_state.read_buf_pos_in, s, (sizeof(esp_state.read_buf)-(esp_state.read_buf_pos_in+1)));
		len -= (sizeof(esp_state.read_buf)-(esp_state.read_buf_pos_in+1));
		esp_state.read_buf_pos_in = 0;
		cu_esp_txl(s+len, len);//recursion to handle the remaining string(to recheck if we will overfill the buffer)
		return;
	}
	memcpy((char *)esp_state.read_buf+esp_state.read_buf_pos_in, s, len);
	esp_state.read_buf_pos_in += len;
}

void cu_esp_txi(sint32 i){
	char buf[8];
	sprintf(buf,"%i",i);
	cu_esp_txp(buf);
}

void cu_esp_txp_error(){ cu_esp_txp((const char*)"ERROR\r\n"); }
void cu_esp_txp_ok(){ cu_esp_txp((const char*)"OK\r\n"); }


sint32 cu_esp_host_serial_start(){

	if(esp_state.host_serial_port != 0)
		cu_esp_host_serial_end();

	auint original_baud = esp_state.baud_rate;
	auint baud = original_baud;

#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	esp_state.host_serial_port = CreateFileA((LPCSTR)esp_state.host_serial_device_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (esp_state.host_serial_port == INVALID_HANDLE_VALUE){
		print_message("ESP ERROR: Host Serial CreateFileA() failed: %u\n", (unsigned int)GetLastError());
		return ESP_SERIAL_OPEN_ERROR;
	}

	// Flush away any bytes previously read or written.
	BOOL success = FlushFileBuffers(esp_state.host_serial_port);
	if (!success){
		print_message("ESP ERROR: Host Serial FlushFileBuffers() failed: %d\n", (DWORD)GetLastError());
		CloseHandle(esp_state.host_serial_port);
		return ESP_SERIAL_OPEN_ERROR;
	}

	COMMTIMEOUTS timeouts = {0};
	timeouts.ReadIntervalTimeout = MAXDWORD; /* this is important for non-blocking without overlapped io */
	timeouts.ReadTotalTimeoutConstant = 0;
	timeouts.ReadTotalTimeoutMultiplier = 0;
	timeouts.WriteTotalTimeoutConstant = 0;
	timeouts.WriteTotalTimeoutMultiplier = 0;

	success = SetCommTimeouts(esp_state.host_serial_port, &timeouts);
	if (!success){
		print_error("ESP ERROR: Host Serial SetCommTimeouts(): %d\n", (DWORD)GetLastError());
		CloseHandle(esp_state.host_serial_port);
		return ESP_SERIAL_OPEN_ERROR;
	}

	// Set the baud rate and other options.
	DCB state = {0};
	state.DCBlength = sizeof(DCB);
	state.BaudRate = baud;
	state.ByteSize = 8;
	state.Parity = NOPARITY;
	state.StopBits = ONESTOPBIT;
	success = SetCommState(esp_state.host_serial_port, &state);
	if (!success){
		print_message("ESP ERROR: Host Serial SetCommState() failed: %d\n", (DWORD)GetLastError());
		CloseHandle(esp_state.host_serial_port);
		return ESP_SERIAL_OPEN_ERROR;
	}
#else
	switch(baud){ //have to convert to a defined constant
		case 2400: baud = B2400; break;
		case 4800: baud = B4800; break;
		case 9600: baud = B9600; break;
		case 19200: baud = B19200; break;
		case 38400: baud = B38400; break;// all after this are not POSIX?! 
		case 57600: baud = B57600; break;
		case 115200: baud = B115200; break;
		case 230400: baud = B230400; break;
		case 460800: baud = B460800; break;
		case 500000: baud = B500000; break;
		case 576000: baud = B576000; break;
		case 921600: baud = B921600; break;
		case 1000000: baud = B1000000; break;
		case 1152000: baud = B1152000; break;
		case 1500000: baud = B1500000; break;
		case 2000000: baud = B2000000; break;
		case 2500000: baud = B2500000; break;
		case 3000000: baud = B3000000; break;
		case 3500000: baud = B3500000; break;
		case 4000000: baud = B4000000; break;
		default:
			print_message("ESP ERROR: Host Serial baud conversion failed, [%d] is not supported\n", original_baud);
			return ESP_SERIAL_OPEN_ERROR;
			break;
	}

	struct termios port_settings;
	memset(&port_settings, 0, sizeof(port_settings));
	port_settings.c_cflag = CS8 | 0 | 0 | CLOCAL | CREAD;//8|N|1|...
	port_settings.c_iflag = IGNPAR;
	port_settings.c_oflag = 0;
	port_settings.c_lflag = 0;
	port_settings.c_cc[VMIN] = 0;// block untill n bytes are received
	port_settings.c_cc[VTIME] = 0;// block untill a timer expires (n * 100 mSec.)	
	cfsetispeed(&port_settings, B115200);

	esp_state.host_serial_port = open((char *)esp_state.host_serial_device_name, O_RDWR | O_NOCTTY | O_NDELAY);

	if(esp_state.host_serial_port == -1){
		print_message("ESP ERROR: Host Serial open() failed: %d\n", (int)cu_esp_get_last_error());
		return ESP_SERIAL_OPEN_ERROR;
	}
	baud = original_baud;
#endif

	print_message("ESP Host Serial initialized: %s @ %d\n", esp_state.host_serial_device_name, original_baud);

	return 0;
}


void cu_esp_host_serial_end(){
	
}

void cu_esp_host_serial_write(uint8 c){
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	DWORD written;
	BOOL success = WriteFile(esp_state.host_serial_port, &c, 1, &written, NULL);

	if(!success || written != 1)
		print_message("ESP ERROR: failed to write host serial byte\n");
#else
	ssize_t r = write(esp_state.host_serial_port, &c, 1);
	if(r != 1){
		print_message("ESP ERROR: failed to write host serial byte\n");
	}
#endif
}

uint8 cu_esp_host_serial_read(){
	uint8 c[2];
#if defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
	DWORD received;
	BOOL success = ReadFile(esp_state.host_serial_port, c, 1, &received, NULL);
	if (!success){
		print_message("ESP ERROR: failed to read from host serial port: %d\n", cu_esp_get_last_error());
		return 0;
	}
#else
	ssize_t received = read(esp_state.host_serial_port, c, 1);
	if(received == -1){
		print_message("ESP ERROR: failed to read from host serial port: %d\n", cu_esp_get_last_error());
		return 0;
	}
#endif
	//return received;
	return 0;
}
