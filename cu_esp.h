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



#ifndef CU_ESP_H
#define CU_ESP_H


#include "types.h"
//#include <stdio.h>

#ifdef _WIN32
	#include <winsock2.h>
#else /* POSIX system(Linux, OSX, etc) */
	#include <sys/socket.h>
	#include <sys/ioctl.h>
//	#include <sys/types.h>
	#include <netinet/in.h>
	#include <netinet/tcp.h>
	#include <arpa/inet.h>
	#include <netdb.h>
	#include <unistd.h>
	#include <errno.h>
#endif










#define ESP_AP_CONNECTED		1	//connected to wifi so AT+CIPSEND is valid
#define ESP_AWAITING_SEND		2	//
#define ESP_START_UP			8	//rebooting
#define ESP_READY			16	//module is ready for commands
#define ESP_MUX				32	//multiple connections mode is active(changes some syntax and adds restrictions)
#define ESP_ECHO			64	//echo back received UART data
#define ESP_PROTO_TCP			128	//standard TCP protocol
#define ESP_PROTO_UDP			256	//standard UDP protocol
#define ESP_ALL_CONNECTIONS	512	//
#define ESP_INTERNET_ACCESS	1024	//not used?
#define ESP_LIST_APS			2048	//currently waiting to spit out simulated AP list
#define ESP_CIPDINFO			4096 //show ip and port information with +IPD messages
#define ESP_SMARTCONFIG_ACTIVE	4096*2
#define ESP_AUTOCONNECT		4096*2*2

#define ESP_AS_AP			1	//softAP only mode(cannot be connected to wifi, some restrictions)
#define ESP_AS_STA			2	//station only mode(cannot be softAP)
#define ESP_AS_AP_STA			3	//softAP and station mode(some restrictions)

#define ESP_MODE_TEXT			0	//normal AT command set

#define ESP_USER_MODE_AT		0 /* normal AT command mode */
#define ESP_USER_MODE_SEND		1 /* send mode, user species the number of bytes and we wait until they are all received to send */
#define ESP_USER_MODE_UNVARNISHED	2 /* user writes as much as they want during the fixed time period, and it is then sent to the network */
#define ESP_USER_MODE_PASSTHROUGH	3 /* UART data is passed onto a physical UART devices connected to the host machine running the emulator */

#define ESP_FACTORY_DEFAULT_BAUD_BITS	30//92///185 /* 9600 */

#define ESP_BIN_ECHO			1//responds back with 0b10101011
#define ESP_BIN_CONNECT		2
#define ESP_BIN_DISCONNECT		3
#define ESP_BIN_SEND			4
#define ESP_BIN_SEND_BIG		5//16 bit payload length specifer

#ifdef _WINDOWS
#define ESP_SERIAL_OPEN_ERROR		INVALID_HANDLE_VALUE
#else
#define ESP_SERIAL_OPEN_ERROR		-1
#endif

#define ESP_UART_AWAITING_COMMAND		4096
#define ESP_UART_AWAITING_TIME			8192
#define ESP_UART_AWAITING_PAYLOAD		16384

#define ESP_DID_FIRST_TICK				32768//thread has started and did first time load stuff
#define ESP_UZEBOX_ACKNOWLEDGED_THREAD	65536//uzebox waited until we set first tick, then sets this to let us know it is continuing emulation

//Timing(some of this is just guesses from memory with no research, TODO)

#define	ESP_UZEBOX_CORE_FREQUENCY		28636363UL//(8*3.579545)*1000000//cycles per second
#define	ESP_RESET_BOOT_DELAY			ESP_UZEBOX_CORE_FREQUENCY+(ESP_UZEBOX_CORE_FREQUENCY>>2)//how long after a reset it takes to become ready
#define	ESP_AT_MS_DELAY			ESP_UZEBOX_CORE_FREQUENCY/1000UL//1 millisecond
#define	ESP_AT_OK_DELAY			ESP_AT_MS_DELAY//how quickly a response to a simply command like "AT\r\n" takes to send "OK\r\n"
#define	ESP_AT_CWLAP_DELAY			4000UL*ESP_AT_MS_DELAY
#define	ESP_AT_CWLAP_INTER_DELAY		6UL*ESP_AT_MS_DELAY
#define	ESP_AT_CWJAP_DELAY			1800UL*ESP_AT_MS_DELAY
#define	ESP_UNVARNISHED_DELAY			20UL*ESP_AT_MS_DELAY
#define	ESP_AT_RST_DELAY			ESP_AT_OK_DELAY

#define ESP_SOCKET_ERROR		-1
#define ESP_INVALID_SOCKET		-1
#ifdef _WIN32
#define ESP_EAGAIN			WSAEWOULDBLOCK
#define ESP_WOULD_BLOCK		WSAEWOULDBLOCK//for our purposes, just means the reason the function failed is there is no data available(or connection requests)
#define ESP_ECONNREFUSED		WSAECONNREFUSED
#else
#define ESP_EAGAIN			EAGAIN
#define ESP_WOULD_BLOCK		EWOULDBLOCK
#define ESP_ECONNREFUSED		ECONNREFUSED
#endif

const char *start_up_string = "ets Mar 12 2023,rst cause 4, boot mode(3,7)\r\n\nwdt reset\r\nload 0x401000000,len 24444,room 16\r\nchksum 0xe0 ho 0 tail 12 room 4\r\nready\r\n";
const char *at_gmr_string = "AT Version:CUzeBox ESP8266 AT Driver 0.5\r\nSDK version: ESP8266 SDK 1.2.0\r\nuzebox.org\r\nBuild:1.0\r\n";


const char *fake_ap_name[] = {
"Linksys01958",
"The Promised LAN!!",
"Put Your Best Port Forward",
"Cats Against Schrodinger",
"Pretty Fly For A Wifi",
"TellMyWifiLoveHer",
"CenturyLink22840",
"LANDownUnder",
"NetGear009",
};


#define ESP_NUM_FAKE_APS	9

const char *ESP_OK_STR  = "OK\r\n";
const char *ESP8266_CORE_VERSION = "v0.3";

typedef struct{


			uint32	rx_await_bytes;//number of bytes we are waiting for, as a "AT+CIPSEND..." payload
		uint32	rx_await_time;//cycles elapsed counting towards "unvarnished mode" packet interval

			uint8	cmd_buf[16UL*1024UL];//non-volatile buffer that all commands are brought to before the many string compares


	

	uint32			server_timer;//the amount of time we have waited for an accept()
	uint32			server_timeout;//the time to wait on accepting a connection(real thing is not indefinite, limit is ~7200 seconds)
	uint32			wifi_timer;//the time from reset(previously connected) or a "AT+JAP" to actually finish connecting and send connect message
	uint32			wifi_delay;//the time it takes to finish connecting to wifi 



	uint32		active_socket;//the current socket for send operations, stored after send commands while we await the payload
	
	uint8		rx_packet[16UL*1024UL];//buffer for received network packets
	uint32		rx_packet_bytes;//how many bytes are left in the packet
	uint8		reset_pin;
	uint8		reset_pin_prev;
	uint8		pad;
	uint8		rf_power;

	/*volatile*/ uint32		state;
	uint8			flash_dirty;
	uint8			ready;







	auint		user_input_mode;//AT, send mode(awaiting predetermined bytes), unvarnished(awaiting predetermined time period)
	auint		baud_divisor;//baud divisor of the Uzebox
	auint		baud_divisor_module;//baud divisor of the ESP
	uint8		write_enabled;
	uint8		read_enabled;
	auint		write_ready_cycle;//the next cycle a new write will be ready
	auint		read_ready_cycle;//the next cycle a new read could be ready(if data available)
	auint		unvarnished_end_cycle;//cycle when the 50hz automatic net send window expires
	auint		unvarnished_bytes;//the number of bytes received for far, in the current timing window
	auint		remaining_send_bytes;//number of bytes left on a send command
	auint		busy_ready_cycle;//when processing a command, the cycle it will be done(and UART can be handled again)
	sint8		write_buf[1024*4];//bytes the Uzebox sends to the ESP8266
	sint8		read_buf[1024*4];//bytes the ESP8266 sends to the Uzebox
	auint		write_buf_pos_in;
	auint		read_buf_pos_in;
	auint		read_buf_pos_out;
	sint8		last_read_byte;//if Uzebox reads when no new byte is sent, keep sending the last one
	sint32		socks[5];
	auint		proto[5];

	auint		delay_pos[8];//the position in read_buf[] that is stalled on, until the timer expires
	auint		delay_len[8];//the length of each stall, combined with position, lets us write out combinations of transmission and delays

	auint		last_cycle_tick;
	auint		ip_delay_timer;//the cycle delay until "WIFI GOT IP\r\n"
	auint		join_delay_timer;//the cycle delay until ""

	auint		uart_ucsr0a, uart_ucsr0b, uart_ucsr0c, uart_ubrr0l, uart_ubrr0h, uart_scramble;
	uint8		uart_double_speed, uart_synchronous, uart_data_bits, uart_rx_enabled, uart_tx_enabled;
	uint8		uart_parity, uart_stop_bits;
	auint		uart_baud_bits; /* the combined bit value of UBRR0L and UBRR0H */
	auint		uart_baud_bits_module; /* the equivalent for the module's current speed(which may be different than the 644) */

	auint		uart_baud_bits_module_default; /* the value to use after a module reset */
#ifdef _WINDOWS
	HANDLE		host_serial_port;
#else
	sint32		host_serial_port;
#endif
	sint8		*host_serial_dev;
	uint8		host_serial_enabled;
#ifdef _WINDOWS
	auint		winsock_enabled;
#endif
	uint32		uart_at_mode;//waiting for an AT command, waiting for bytes for an AT+CIPSEND, or waiting some milliseconds to send packet for AT+CIPSENDEX?
	uint32		uart_at_state;//0 = AT, 1 = binary mode
	uint8 		baud_bits0,baud_bits1;

	uint32		send_to_socket;	//the current socket an "AT+CIPSEND/X" is dealing with, that is, where to send the payload when received
	uint32		cip_mode;

	sint32		listen_socket;
	uint8		tcp_send_buf[4][2048];
	uint32		tcp_seg_id[4];//reset with "AT+CIPBUFRESET"
	sint32		ping_sock;
	uint32		protocol[5];
	struct sockaddr_in	sock_info[5];

	volatile uint32_t	busy_cycles;

	sint8		wifi_name[64];
	sint8		wifi_pass[64];
	sint8		wifi_mac[32];
	sint8		wifi_ip[24];

	sint8		soft_ap_name[64];
	sint8		soft_ap_pass[64];
	sint8		soft_ap_mac[32];
	sint8		soft_ap_ip[24];
	uint32		soft_ap_channel;
	uint32		soft_ap_encryption;

	sint8		translink_host[128];
	uint8		translink_proto;
	uint16		translink_port;

	sint8		station_mac[32];//todo
	sint8		station_ip[24];

}cu_state_esp_t;


/*
** Resets ESP8266 peripheral. Cycle is the CPU cycle when it happens which
** might be used for emulating timing constraints.
*/
void  cu_esp_reset(auint cycle);


/*
** Sets chip select's state, TRUE to enable, FALSE to disable.
*/
//void  cu_spir_cs_set(boole ena, auint cycle);


/*
** Sends a byte of data to the ESP8266. The passed cycle corresponds the cycle
** when it was clocked out of the AVR.
*/
void  cu_esp_send(auint data, auint cycle);


/*
** Receives a byte of data from the ESP8266. The passed cycle corresponds the
** cycle when it must start to clock into the AVR. 0xFF is sent when the card
** tri-states the line.
*/
auint cu_esp_recv(auint cycle);


/*
** Returns ESP8266 state. It may be written, then the cu_esp_update()
** function has to be called to rebuild any internal state depending on it.
*/
cu_state_esp_t* cu_esp_get_state(void);


/*
** Rebuild internal state according to the current state. Call after writing
** the ESP8266 state.
*/
void cu_esp_clear_at_command();
void cu_esp_reset_pin(uint8 state, auint cycle);
void cu_esp_uzebox_write(uint8 val, auint cycle);
auint cu_esp_uzebox_read(auint cycle);
void cu_esp_uzebox_modify(auint port, auint val, auint cycle); /* UART settings have been modified on the 644(fake for now) */
auint cu_esp_uzebox_read_ready(auint cycle);
auint cu_esp_uzebox_write_ready(auint cycle);
void cu_esp_reset_network();
auint cu_esp_verify_mac_string(auint pos);
void  cu_esp_external_write(uint8 val); /* write to a real serial device on the host machine, instead of the emulated ESP8266 */
void  cu_esp_external_read();
auint cu_esp_net_last_error();
sint32  cu_esp_net_connect(sint8 *hostname, uint32 sock, uint32 port, uint32 type);
sint32  cu_esp_net_send(uint32 sock, uint8 *buf, sint32 len, sint32 flags);
sint32  cu_esp_net_recv(uint32 sock, uint8 *buf, sint32 len, sint32 flags);
void cu_esp_net_send_unvarnished(sint8 *buf, auint len);
auint cu_esp_atoi(char *str);
sint32 cu_esp_get_last_error();
sint32 cu_esp_init_sockets();
void cu_esp_net_cleanup(); //tear down anything in use, exiting program
void cu_esp_close_socket(uint32 sock);
void cu_esp_reset_uart();
void  cu_esp_update(void);
void  cu_esp_txi(sint32 i);
void  cu_esp_txp(const char *s);
void cu_esp_txl(const char *s, auint len);
void  cu_esp_txp_ok();
void  cu_esp_txp_error();
void  cu_esp_timed_stall(auint cycles);
auint cu_esp_update_timer_counts(auint cycle);
sint32  cu_esp_process_ipd();
void  cu_esp_save_config();
auint  cu_esp_load_config();
void  cu_esp_process_at();
/*
** AT and network related commands
*/
void cu_esp_at_at();
void cu_esp_at_ate(sint8 *cmd_buf);
void cu_esp_at_rst();
void cu_esp_at_gmr();
void cu_esp_at_gslp(sint8 *cmd_buf);

	
void cu_esp_at_ciobaud(sint8 *cmd_buf);
void cu_esp_at_cwmode(sint8 *cmd_buf);
void cu_esp_at_cwjap(sint8 *cmd_buf);
void cu_esp_at_cwlap(sint8 *cmd_buf);
void cu_esp_at_cwqap(sint8 *cmd_buf);
void cu_esp_at_cwsap(sint8 *cmd_buf);
void cu_esp_at_cwlif(sint8 *cmd_buf);
void cu_esp_at_cwdhcp(sint8 *cmd_buf);
void cu_esp_at_cipstamac(sint8 *cmd_buf);
void cu_esp_at_cipapmac(sint8 *cmd_buf);
void cu_esp_at_cipsta(sint8 *cmd_buf);
void cu_esp_at_cipap(sint8 *cmd_buf);
	
void cu_esp_at_cipstatus(sint8 *cmd_buf);
void cu_esp_at_cipstart(sint8 *cmd_buf);
void cu_esp_at_cipsend(sint8 *cmd_buf);
void cu_esp_at_cipclose(sint8 *cmd_buf);
void cu_esp_at_cifsr(sint8 *cmd_buf);
void cu_esp_at_cipmux(sint8 *cmd_buf);
void cu_esp_at_cipserver(sint8 *cmd_buf);
void cu_esp_at_cipmode(sint8 *cmd_buf);
void cu_esp_at_cipsto(sint8 *cmd_buf);
void cu_esp_at_ciupdate(sint8 *cmd_buf);
void cu_esp_at_ipr(sint8 *cmd_buf);

void cu_esp_at_uart(sint8 *cmd_buf);
void cu_esp_at_uart_cur(sint8 *cmd_buf);
void cu_esp_at_rfpower(sint8 *cmd_buf);
void cu_esp_at_rfvdd(sint8 *cmd_buf);
void cu_esp_at_restore(sint8 *cmd_buf);
void cu_esp_at_cipdinfo(sint8 *cmd_buf);
void cu_esp_at_ping(sint8 *cmd_buf);
void cu_esp_at_cwautoconn(sint8 *cmd_buf);
void cu_esp_at_savetranslink(sint8 *cmd_buf);
void cu_esp_at_cipbufreset(sint8 *cmd_buf);
void cu_esp_at_cipcheckseq(sint8 *cmd_buf);
void cu_esp_at_cipbufstatus(sint8 *cmd_buf);
void cu_esp_at_cipsendbuf(sint8 *cmd_buf);
void cu_esp_at_sendex(sint8 *cmd_buf);
void cu_esp_at_cwstartsmart(sint8 *cmd_buf);
void cu_esp_at_cwstopsmart(sint8 *cmd_buf);
	
void cu_esp_at_bad_command();
void cu_esp_at_debug(sint8 *cmd_buf);


void cu_esp_reset_factory();
void cu_esp_save_translink();
void cu_esp_save_uart();
void cu_esp_save_station_mac();
void cu_esp_save_station_ip();
void cu_esp_save_soft_ap_mac();
void cu_esp_save_soft_ap_ip();
void cu_esp_save_soft_ap_credentials();
void cu_esp_save_dhcp();
void cu_esp_save_mode();
void cu_esp_save_wifi_credentials();
void cu_esp_save_auto_conn();

/* emulator only AT commands */
void cu_esp_at_hserial_start(sint8 *cmd_buf);
void cu_esp_at_hserial_stop(sint8 *cmd_buf);
void cu_esp_at_hserial_send(sint8 *cmd_buf);
void cu_esp_at_hserial_read(sint8 *cmd_buf); /* should we also support transparent mode? */

sint32 cu_esp_host_serial_start(sint8 *dev, auint baud);
void cu_esp_host_serial_end();
void cu_esp_host_serial_write(uint8 c);
uint8 cu_esp_host_serial_read();

#endif
