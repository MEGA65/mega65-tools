/**
 * @file defs.h
 * @brief Data type definitions and helpful macros.
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

#ifndef __DEFSH__
#define __DEFSH__

#include <stdint.h>

/*
 * Data types.
 */

#if defined(__CPIK__)
#define uint32_t unsigned long long int
#define uint16_t unsigned long int

#define int32_t long long int
#define int16_t long int
#define int8_t int
#else
// #define uint32_t unsigned long int
// #define uint16_t unsigned short int

// #define int32_t long int
// #define int16_t short int
// #define int8_t char
#endif

//#define uint8_t unsigned char
#define byte_t unsigned char


// 32-bit buffer_t to allow RX buffer to be outside of the first 64KB
// (all accesses to it are via DMA, so this isn't a problem)
typedef uint32_t buffer_t;
// localbuffer_t is for buffers in the same 64KB RAM bank as the running program
typedef unsigned char * localbuffer_t;
typedef char * string_t;

typedef enum {
   FALSE = 0,
   TRUE
} bool_t;

typedef union {
   uint32_t d;
   uint16_t w[2];
   byte_t b[4];
} _uint32_t;

#if !defined(NULL)
#define NULL ((void*)0)
#endif

#define bit(X) unsigned X:1

#define __rom        const rom

/*
 * Useful macros.
 */

#define LOW(x) (x & 0xff)
#define HIGH(x) (x >> 8)

#define set_bit(X, Y)         X |= (1 << (Y))
#define clear_bit(X, Y)       X &= (~(1 << (Y)))
#define toggle_bit(X, Y)      X ^= (1 << (Y))
#define test_bit(X, Y)        (X & (1 << (Y)))

#define for_each(ARRAY, PTR)  for(PTR=ARRAY; ((uint16_t)(PTR)-(uint16_t)(ARRAY))<sizeof(ARRAY); PTR++)
#define forever()             for(;;)

#endif
