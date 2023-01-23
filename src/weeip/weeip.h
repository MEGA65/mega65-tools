/**
 * @file weeip.h
 * @brief Weeip library. Global definitions and configuration.
 */

/********************************************************************************
 ********************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 1995-2013 Bruno Basseto (bruno@wise-ware.org).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ********************************************************************************
 ********************************************************************************/

#ifndef __WEEIPH__
#define __WEEIPH__

#include "defs.h"
#include "task.h"
#include "inet.h"

/**
 * Number of sockets of the system.
 */
#define MAX_SOCKET         4

// XXX - TIMEOUT_TCP _must_ be > 1, or else all TX will be deemed
// re-transmissions, and the data length will be subtracted from the sequence
// number, resulting in -ve sequence numbers which confuses everything.
#define TIMEOUT_TCP			2
#define RETRIES_TCP			30
#define TICK_TCP			   44					// one second

/**
 * Communication events reported to socket callbacks.
 */
typedef enum {
   WEEIP_EV_NONE = 0,
   WEEIP_EV_CONNECT,                      ///< Connection established.
   WEEIP_EV_DISCONNECT,                   ///< Disconnection from peer.
   WEEIP_EV_DISCONNECT_WITH_DATA,         ///< Disconnection from peer, but packet also contains data
   WEEIP_EV_DATA,                         ///< Data arrival.
   WEEIP_EV_DATA_SENT                     ///< Data sent.
} WEEIP_EVENT;

#define SOCKET_FREE			0
typedef enum {
	SOCKET_UDP = 1,
	SOCKET_TCP
} WEEIP_PROTOCOL;

/**
 * Transport Control Protocol state machine.
 */
typedef enum {
	_IDLE = 0,
	_LISTEN,
	_SYN_SENT,
	_SYN_REC,
	_ACK_REC,
	_CONNECT,
	_ACK_WAIT,
	_FIN_SENT,
	_FIN_REC,
	_FIN_ACK_REC
} TCP_STATE;

/**
 * SOCKET opaque data structure.
 */
#undef SOCKET
typedef struct {
  // XXX CC65 doesn't support packed bitfields properly,
  // so we use full bytes for all these.
	unsigned type;                         ///< Socket usage and protocol.
	unsigned listening;                    ///< Listening flag.
	unsigned timeout;                      ///< Timeout flag.
	unsigned time;
	unsigned state;                        ///< TCP state machine.
	unsigned retry;                        ///< Retry counter.
	byte_t toSend;                            ///< Flags to send on next packet.
        buffer_t rx;                              ///< Reception buffer pointer.
	void *tx;                                 ///< Transmission buffer pointer.

       
        uint32_t rx_size;                         ///< Reception buffer size.
	uint16_t tx_size;                         ///< Size of transmit packet.
	uint32_t rx_data;                         ///< Bytes used in RX buffer (i.e., bytes of data available)
        uint32_t rx_oo_start;                     ///< Start of out-of-order held data
        uint32_t rx_oo_end;                       ///< End of out-of-order held data
  
  
	task_t callback;                          ///< Task for socket management.
	uint16_t port;                            ///< Local port number.
	uint16_t remPort;                         ///< Remote port number.
	IPV4 remIP;                               ///< Remote IP address.
	_uint32_t seq;                            ///< Local sequence number.
	_uint32_t seqStart;                       ///< Local sequence number.
	_uint32_t remSeq;                         ///< Remote sequence number.
	_uint32_t remSeqStart;                    ///< Initial remote sequence number.

        // TCP Selective ACK (SACK) tracking
        uint8_t sack_blocks;
        uint32_t sack_block_0_left;
        uint32_t sack_block_0_right;
        uint32_t sack_block_1_left;
        uint32_t sack_block_1_right;
} SOCKET;

extern SOCKET *_sckt;
extern SOCKET _sockets[MAX_SOCKET];
extern HEADER _header;
extern IPV4 ip_local;
extern IPV4 ip_dhcpserver;

// Enable logging of ethernet activity to the MEGA65 serial monitor interface
extern unsigned char eth_log_mode;
#define ETH_LOG_RX 1
#define ETH_LOG_TX 2

//IPV4 ip_address(static char rom *str);
extern SOCKET *socket_create(WEEIP_PROTOCOL protocol);
extern void socket_release(SOCKET *s);
extern void socket_select(SOCKET *s);
extern void socket_set_rx_buffer(buffer_t b, uint32_t size);
extern void socket_set_callback(task_t c);
extern bool_t socket_listen(uint16_t p);
extern bool_t socket_connect(IPV4 *a, uint16_t p);
extern bool_t socket_send(localbuffer_t bdata, int size);
extern uint16_t socket_data_size();
extern void socket_reset();
extern bool_t socket_disconnect();
extern void nwk_downstream();
extern byte_t nwk_upstream(byte_t);
extern byte_t nwk_tick(byte_t sig);
extern void weeip_init();
#endif
