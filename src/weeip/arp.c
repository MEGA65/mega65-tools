/**
 * @file arp.c
 * @brief Address Resolution Protocol implementation.
 * @compiler CPIK 0.7.3 / MCC18 3.36
 * @author Bruno Basseto (bruno@wise-ware.org)
 */

#include <stdio.h>

#include "task.h"
#include "weeip.h"
#include "eth.h"

#include "memory.h"
#include "random.h"

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

#include <string.h>
#include "weeip.h"
#include "eth.h"

/*
 * Opcodes.
 */
#define ARP_REQUEST           0x0100         // big-endian for 0x0001
#define ARP_REPLY             0x0200         // big-endian for 0x0002

/**
 * A known IP-MAC addresses pair.
 */
typedef struct {
   IPV4 ip;                                  ///< IP address.
   EUI48 mac;                                ///< Associated MAC address.
   uint16_t time;                            ///< Time to expire.
} ARP_CACHE_ENTRY;

#define MAX_CACHE             8              ///< ARP Cache table size.

#define ARP_TICK_TIME         147            // about 10 seconds
#define MAX_TIMEOUT_ARP       120            // about 20 minutes
#define MIN_TIMEOUT_ARP       2              // about 20 seconds

/**
 * List of known MAC addresses.
 */
ARP_CACHE_ENTRY arp_cache[MAX_CACHE];

#define ARP(X) _header.arp.X

/**
 * Search for an IP among the known ones.
 * Starts a new table entry if not found.
 * @param ip Address to look for.
 * @param mac Corresponding MAC address, if found.
 * @return TRUE if found.
 */
bool_t
query_cache
   (IPV4 *ip,
   EUI48 *mac)
{
   ARP_CACHE_ENTRY *i, *s;
   byte_t old;

   /*
    * Checks if broadcast address.
    */
   if(ip->d == 0xffffffff) {
      memset((void*)mac, 0xff, sizeof(EUI48));
      return TRUE;
   }

   /*
    * Loops into arp_cache.
    */
   for_each(arp_cache, i) {
      if(i->ip.d == ip->d) {
         if(i->mac.b[0] == 0xff) return FALSE;
         memcpy((void*)mac, (void*)&i->mac, sizeof(EUI48));
#if 0
	 printf("IP %d.%d.%d.%d is in ARP cache\n",
		ip->b[0],ip->b[1],ip->b[2],ip->b[3]);
#endif
         return TRUE;
      }
   }
#if 0
   printf("IP %d.%d.%d.%d not in ARP cache\n",
	  ip->b[0],ip->b[1],ip->b[2],ip->b[3]);
#endif
   /*
    * Unknown IP.
    * Look for an empty entry.
    */
   old = 0xff;
   s = arp_cache;
   for_each(arp_cache, i) {
      if(i->time <= old) {
         /*
          * Search the oldest one, in case we do not find an unused entry.
          */
         old = i->time;
         s = i;
      }
      if(old == 0)
         /*
          * Done: found an empty entry.
          */
         break;
   }

   /*
    * Init the entry with the desired IP.
    */
   s->ip.d = ip->d;
   s->mac.b[0] = 0xff;
   s->time = MIN_TIMEOUT_ARP;
   return FALSE;
}

/**
 * Update IP information into the ARP cache.
 * @param ip IP Address, must already be into the cache.
 * @param mac MAC address to update.
 */
void
update_cache
   (IPV4 *ip,
   EUI48 *mac)
{
   ARP_CACHE_ENTRY *i;

#if 0
   printf("ARP: IP %d.%d.%d.%d is at %x:%x:%x:%x:%x:%x\n",
	  ip->b[0],ip->b[1],ip->b[2],ip->b[3],
	  mac->b[0],mac->b[1],mac->b[2],mac->b[3],mac->b[4],mac->b[5]
	  );
#endif

   for_each(arp_cache, i) {
      if(i->ip.d == ip->d) {
         memcpy((void*)&i->mac, (void*)mac, sizeof(EUI48));
         i->time = MAX_TIMEOUT_ARP;
         return;
      }
   }

   // Not an existing entry in the cache to be updated, so replace a random entry?
   i = &arp_cache[random32(MAX_CACHE)];
   i->ip.d = ip->d;
   memcpy((void*)&i->mac, (void*)mac, sizeof(EUI48));
   return;
}

/**
 * Send a ARP QUERY message to find about an IP address.
 * @param IP address to find.
 */
void
arp_query
   (IPV4 *ip)
{
   /*
    * ARP REQUEST message.
    */
   ARP(hardware) = 0x0100;                      // ethernet (big-endian for 0x0001)
   ARP(protocol) = 0x0008;                      // internet protocol (big-endian for 0x0800)
   ARP(hw_size) = 6;                            // 6 bytes for ethernet MAC
   ARP(pr_size) = 4;                            // 4 bytes for IPv4
   ARP(opcode) = ARP_REQUEST;

   /*
    * Local addresses for the Sender.
    */
   memcpy((void*)&ARP(orig_hw), (void*)&mac_local, sizeof(EUI48));
   ARP(orig_ip).d = ip_local.d;

   /*
    * Destination addresses.
    */
   ARP(dest_ip).d = ip->d;                                  // who we are looking for
   memset(&ARP(dest_hw), 0xff, sizeof(EUI48));              // what we want to know

   /*
    * Send message.
    */
   eth_arp_send(&ARP(dest_hw));
}

/**
 * Process an incoming ARP message.
 */
void
arp_mens
   (void)
{
   static EUI48 mac;

   /*
    * Check opcode.
    */

   if(ARP(opcode) == ARP_REQUEST) {
      printf("ARP request %d.%d.%d.%d\n",ARP(dest_ip).b[0],ARP(dest_ip).b[1],ARP(dest_ip).b[2],ARP(dest_ip).b[3]);
      /*
       * Address request.
       * Check local address.
       */
      if(ARP(dest_ip).d != ip_local.d) return;

      /*
       * Looking for us.
       * Insert sender address into cache.
       */
      query_cache(&ARP(orig_ip), &mac);
      update_cache(&ARP(orig_ip), &ARP(orig_hw));

      /*
       * Assemble a response message.
       */
      ARP(hardware) = 0x0100;          // ethernet (big-endian for 0x0001)
      ARP(protocol) = 0x0008;          // internet protocol (big-endian for 0x0800)
      ARP(hw_size) = 6;                // 6 bytes for ethernet MAC
      ARP(pr_size) = 4;                // 4 bytes for IPv4
      ARP(opcode) = ARP_REPLY;

      /*
       * Swap addresses.
       */
      memcpy((void*)&ARP(dest_hw), (void*)&ARP(orig_hw), sizeof(EUI48));
      ARP(dest_ip).d = ARP(orig_ip).d;

      /*
       * Local addresses as Sender.
       */
      memcpy((void*)&ARP(orig_hw), (void*)&mac_local, sizeof(EUI48));
      ARP(orig_ip.d) = ip_local.d;

      /*
       * Send answer.
       */
      eth_arp_send(&ARP(dest_hw));
   } else if(ARP(opcode) == ARP_REPLY) {
      /*
       * ARP response.
       */
      update_cache(&ARP(orig_ip), &ARP(orig_hw));
   }
}

/**
 * ARP timing control task.
 * Called each 10 seconds.
 */
byte_t
arp_tick
   (byte_t p)
{
   ARP_CACHE_ENTRY *i;

   for_each(arp_cache, i) {
      if(i->time) {
         i->time--;
         if(i->time == 0) {
            /*
             * Entry too old, remove it.
             */
            memset((void*)i, 0xff, sizeof(ARP_CACHE_ENTRY));
            i->time = 0;
         }
      }
   }

   /*
    * Reschedule for periodic execution.
    */
   task_add(arp_tick, ARP_TICK_TIME, 0,"arptick");
   return 0;
}

/**
 * ARP setup and startup.
 */
void
arp_init
   (void)
{
   ARP_CACHE_ENTRY *i;
   memset((void*)&arp_cache, 0xff, sizeof(arp_cache));
   for_each(arp_cache, i) {
      i->time = 0;
   }
   task_add(arp_tick, ARP_TICK_TIME, 0,"arptick");
}
