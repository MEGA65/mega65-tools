/**
 * @file inet.h
 * @brief Weeip library. Data types and structures for IP communication.
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

#ifndef __INETH__
#define __INETH__
#include "defs.h"

#define HEADER_LEN 64

/**
 * Internet protocol address (version 4).
 */
typedef union {
   uint32_t d;
   byte_t b[4];
} IPV4;

/**
 * Ethernet MAC address.
 */
typedef struct {
   byte_t b[6];
} EUI48;

/**
 * ARP message format.
 */
typedef struct {
   uint16_t hardware;
   uint16_t protocol;                  
   byte_t hw_size;                  
   byte_t pr_size;                  
   uint16_t opcode;                  
   EUI48 orig_hw;         
   IPV4 orig_ip;            
   EUI48 dest_hw;         
   IPV4 dest_ip;            
} ARP_HDR;

/**
 * IP Header format.
 */
typedef struct {
   byte_t ver_length;            ///< Protocol version (4) and header size (32 bits units).
   byte_t tos;                   ///< Type of Service.
   uint16_t length;              ///< Total packet length.
   uint16_t id;                  ///< Message identifier.
   uint16_t frag;                ///< Fragmentation index (not used).
   byte_t ttl;                   ///< Time-to-live.
   byte_t protocol;              ///< Transport protocol identifier.
   uint16_t checksum;            ///< Header checksum.
   IPV4 source;                  ///< Source host address.
   IPV4 destination;             ///< Destination host address.
} IP_HDR;

/**
 * ICMP header format.
 */
typedef struct {
   byte_t type;
   byte_t fcode;
   uint16_t checksum;
   uint16_t id;
   uint16_t seq;
} ICMP_HDR;

/**
 * TCP header format.
 */
typedef struct {
   uint16_t source;              ///< Source application address.
   uint16_t destination;         ///< Destination application address.
   _uint32_t n_seq;              ///< Output stream sequence number.
   _uint32_t n_ack;              ///< Input stream sequence number.
   byte_t hlen;                  ///< Header size.
   byte_t flags;                 ///< Protocol flags.
   uint16_t window;              ///< Reception window buffer (not used).
   uint16_t checksum;            ///< Packet checksum, with pseudo-header.
   uint16_t urgent;              ///< Urgent data pointer (not used).
   uint8_t options[HEADER_LEN - 40];           ///< TCP options
} TCP_HDR;

/**
 * UDP header format.
 */
typedef struct {
   uint16_t source;              ///< Source application address.
   uint16_t destination;         ///< Destination application address.
   uint16_t length;              ///< UDP packet size.
   uint16_t checksum;            ///< Packet checksum, with pseudo-header.
} UDP_HDR;

/*
 * Transport Control Protocol flags.
 */
#define URG      0x20            ///< Urgent pointer valid (not used).
#define ACK      0x10            ///< ACK field valid.
#define PSH      0x08            ///< Push data.
#define RST      0x04            ///< Reset connection.
#define SYN      0x02            ///< Synchronize (connection startup).
#define FIN      0x01            ///< Finalize (connection end).

/*
 * Values for protocol field.
 */
#define IP_PROTO_TCP      6      ///< TCP packet.
#define IP_PROTO_UDP      17     ///< UDP packet.
#define IP_PROTO_ICMP   1        ///< ICMP packet.

#undef NTOHS
#undef HTONS
#define NTOHS(x) (((uint16_t)x >> 8) | ((uint16_t)x << 8))
#define HTONS(x) (((uint16_t)x >> 8) | ((uint16_t)x << 8))

/**
 * General message header structure.
 * 40 bytes are required for bare TCP.
 * Extra size allows for TCP option bytes that we need.
 * This includes MSS negotiation as well as SACK.
 */
typedef union {
   byte_t b[HEADER_LEN];                 ///< Raw byte access.
   ARP_HDR arp;                  ///< ARP message access.
   struct {
      IP_HDR ip;                 ///< IP header access.
      union {   
         ICMP_HDR icmp;            ///< ICMP header access.
         TCP_HDR tcp;            ///< TCP header access.
         UDP_HDR udp;            ///< UDP header access.
      } t;
   };
} HEADER;

#endif
