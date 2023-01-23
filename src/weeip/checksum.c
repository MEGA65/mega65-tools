/**
 * @file checksum.c
 * @brief Helper functions for calculating IP checksums.
 * @compiler CPIK 0.7.3 / MCC18 3.36
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

#include "task.h"
#include "checksum.h"

/**
 * Last checksum computation result.
 */
chks_t chks;

static byte_t _a, _b, _c;
static unsigned int _b16;

/**
 * Sums a 16-bit word to the current checksum value.
 * Optimized for 8-bit word processors.
 * The result is found in chks.
 * @param v Value to sum.
 */
void
add_checksum
   (uint16_t v)
{
   /*
    * First byte (MSB).
    */
   _a = chks.b[0];
   _b = _b16 = _a + (HIGH(v));   
   _c = _b16 >> 8;
   chks.b[0] = _b;

   /*
    * Second byte (LSB).
    */
   _a = chks.b[1];
   _b = _b16 = _a + (LOW(v)) + _c;
   _c = _b16 >> 8;
   chks.b[1] = _b;

   /*
    * Test for carry.
    */
   if(_c) {
      if(++chks.b[0] == 0) chks.b[1]++;
   }
}

/**
 * Calculate checksum for a memory area (must be word-aligned).
 * Pad a zero byte, if the size is odd.
 * Optimized for 8-bit word processors.
 * The result is found in chks.
 * @param p Pointer to a memory buffer.
 * @param t Data size in bytes.
 */
void 
ip_checksum
   (localbuffer_t p,
   uint16_t t)
{
   _c = 0;

   while(t) {
      /*
       * First byte (do not care if LSB or not).
       */
     _a = chks.b[0];
      _b = _b16 = _a + (*p++) + _c;
      _c = _b16>>8;
      chks.b[0] = _b;

      if(--t == 0) {
         /*
          * Pad a zero. Just test the carry.
          */
         if(_c) {
            if(++chks.b[1] == 0) chks.b[0]++;
         }
         return;
      }

      /*
       * Second byte (do not care if MSB or not).
       */
      _a = chks.b[1];
      _b = _b16 = _a + (*p++) + _c;
      _c = _b16>>8;
      chks.b[1] = _b;

      t--;

   }

   /*
    * Test the carry.
    */
   if(_c) {
     ++chks.b[0];
     if(!chks.b[0]) chks.b[1]++;
   }
}
