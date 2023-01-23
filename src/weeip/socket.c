/**
 * @file socket.c
 * @brief Socket API implementation.
 * @compiler CC65
 * @author Paul Gardner-Stephen (paul@m-e-g-a.org)
 * derived from:
 * @author Bruno Basseto (bruno@wise-ware.org)
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
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "weeip.h"
#include "arp.h"
#include "eth.h"

#include "random.h"

extern uint16_t id;

/**
 * Socket list.
 */
SOCKET _sockets[MAX_SOCKET];

/**
 * Socket currently in use.
 */
SOCKET *_sckt;

/*
 * Local port definitions.
 */
#define PORT_MIN         1024
#define PORT_MAX         4096
static uint16_t port_used;                              ///< Next port to use.

/**
 * Create and initialize a socket.
 * @param protocol Transport Protocol (SOCKET_TCP or SOCKET_UDP).
 * @return Socket identifier, or NULL if there are no more available sockets.
 */
SOCKET*
socket_create
   (WEEIP_PROTOCOL protocol)
{
   /*
    * Find an unused socket.
    */
   for_each(_sockets, _sckt) {
      if(_sckt->type == SOCKET_FREE) goto found;
   }
   
   return NULL;

found:
   /*
    * Initialize socket structure.
    */
   memset((void*)_sckt, 0, sizeof(SOCKET));
   _sckt->type = protocol;
   _sckt->seq.w[0] = rand32(0);
   _sckt->seq.w[1] = rand32(0);
   _sckt->seqStart.w[0]=_sckt->seq.w[0];
   _sckt->seqStart.w[1]=_sckt->seq.w[1];
   return _sckt;
}

/**
 * Finish using a socket.
 * @param s Socket identifier.
 */
void 
socket_release
   (SOCKET *s)
{
   if(s == NULL) return;
   memset((void*)s, 0, sizeof(SOCKET));
}

/**
 * Select a socket for the next operations.
 * @param s Socket identifier.
 */
void 
socket_select
   (SOCKET *s)
{
   _sckt = s;
}

/**
 * Setup a reception buffer for the selected socket.
 * @param b Reception buffer to use.
 * @param size Buffer size in bytes.
 */
void 
socket_set_rx_buffer
   (buffer_t b,
    uint32_t size)
{
   if(_sckt == NULL) return;
   _sckt->rx = b;
   _sckt->rx_size = size;
   _sckt->rx_data = 0;
   _sckt->rx_oo_start = 0;
   _sckt->rx_oo_end = 0;
}

/**
 * Setup a callback task for the selected socket.
 * @param c Task for socket management.
 */
void 
socket_set_callback
   (task_t c)
{
   if(_sckt == NULL) return;
   _sckt->callback = c;
}

/**
 * Put the socket to listen at the specified port.
 * @param p Listening port.
 * @return TRUE if succeed.
 */
bool_t 
socket_listen
   (uint16_t p)
{
   if(_sckt == NULL) return FALSE;
   if(_sckt->type == SOCKET_TCP) {
      if(_sckt->state != _IDLE) return TRUE;
      _sckt->state = _LISTEN;
   } else {
      _sckt->state = _CONNECT;
   }
   
   _sckt->port = HTONS(p);
   _sckt->listening = TRUE;
   return TRUE;
}

/**
 * Ask for a connection to a remote socket.
 * @param a Destination host IP address.
 * @param p Destination port.
 * @return TRUE if succeeded.
 */
bool_t 
socket_connect
   (IPV4 *a,
   uint16_t p)
{
   /*
    * Check socket availability.
    */
   if(_sckt == NULL) return FALSE;
   if((_sckt->type == SOCKET_TCP) 
      && (_sckt->state != _IDLE)) return FALSE;
   
   /*
    * Select a local port number.
    */
   _sckt->port = HTONS(port_used);
   if(port_used == PORT_MAX) port_used = PORT_MIN;
   else port_used++;

   _sckt->remIP.d = a->d;
   _sckt->remPort = HTONS(p);

   if(_sckt->type == SOCKET_UDP) {
      /*
       * UDP socket.
       * No connection procedure needed.
       */
      _sckt->state = _CONNECT;
      return TRUE;
   }

   /*
    * TCP socket.
    * Force sending SYN message.
    */
   _sckt->state = _SYN_SENT;
   _sckt->toSend = SYN;
   _sckt->retry = RETRIES_TCP;
   task_cancel(nwk_upstream);
   task_add(nwk_upstream, 0, 0,"upstream");
   return TRUE;
}

/**
 * Ask for data transmission to the peer.
 * @param data Buffer for the data message.
 * @param size Buffer size in bytes.
 * @return TRUE if succeeded.
 */
bool_t 
socket_send
   (localbuffer_t fdata,
   int size)
{
   if(_sckt == NULL) return FALSE;
   if(_sckt->state != _CONNECT) return FALSE;

   // Check if we still have un-acknowledged data, and
   // if so, return failure
   if (_sckt->toSend & PSH) return FALSE;

   if(_sckt->type == SOCKET_TCP) _sckt->state = _ACK_WAIT;
   
   _sckt->tx = fdata;
   _sckt->tx_size = size;
   _sckt->toSend = ACK | PSH;
   _sckt->retry = RETRIES_TCP;
   task_cancel(nwk_upstream);
   task_add(nwk_upstream, 0, 0,"upstream");
   return TRUE;
}

/**
 * Returns the amount of data available for reading in bytes.
 */
uint16_t
socket_data_size()
{
   if(_sckt == NULL) return 0;
   return _sckt->rx_data;
}

/**
 * Ask for socket disconnection.
 * @return TRUE if succeeded.
 */
bool_t 
socket_disconnect()
{
   if(_sckt == NULL) return FALSE;
   if(_sckt->type == SOCKET_UDP) {
      /*
       * UDP socket.
       * No disconnection procedure.
       */
      _sckt->state = _IDLE;
      return TRUE;
   }

   /*
    * TCP socket.
    * Start sending FIN message.
    */
   //   printf("TCP close\n");
   if(_sckt->state != _CONNECT) return FALSE;
   _sckt->state = _FIN_SENT;
   _sckt->toSend = FIN | ACK;
   _sckt->retry = RETRIES_TCP;

   task_cancel(nwk_upstream);
   task_add(nwk_upstream, 0, 0,"upstream");
   return TRUE;
}

/**
 * Reset a socket, possibly sending a RST message.
 */
void 
socket_reset()
{
   if(_sckt == NULL) return;
   if(_sckt->type != SOCKET_TCP) return;
   if(_sckt->state != _IDLE) {
      _sckt->toSend = RST;
      task_cancel(nwk_upstream);
      task_add(nwk_upstream, 0, 0,"upstream");
   }
   _sckt->state = _IDLE;
}

/**
 * Network system initialization.
 */
void 
weeip_init()
{
   memset(_sockets, 0, sizeof(_sockets));
   _sckt = _sockets;
   port_used = PORT_MIN + (rand16(PORT_MAX - PORT_MIN+1));
   id = rand16(0);
   task_add(nwk_tick, TICK_TCP, 0,"nwktick");
   eth_init();
   arp_init();
}
