#pragma once
/*
 * Minimal Z-library substitution for the redcode/6502 emulator.
 * Set via -DCPU_6502_DEPENDENCIES_H="sid_types.h" so the emulator
 * builds without the full Z library (which doesn't support ARM cross-compilation).
 */

#include <stdint.h>
#include <stddef.h>

/* Basic integer types */
typedef uint8_t          zuint8;
typedef uint16_t         zuint16;
typedef unsigned int     zuint;
typedef size_t           zusize;
typedef uint8_t          zboolean;
typedef int8_t           zsint8;

#ifndef TRUE
#  define TRUE  1
#endif
#ifndef FALSE
#  define FALSE 0
#endif

/* Compiler attribute stubs */
#define Z_API
#define Z_INLINE         inline
#define Z_EMPTY

/* C++ extern "C" guards */
#ifdef __cplusplus
#  define Z_C_SYMBOLS_BEGIN extern "C" {
#  define Z_C_SYMBOLS_END   }
#else
#  define Z_C_SYMBOLS_BEGIN
#  define Z_C_SYMBOLS_END
#endif

/* Packed struct macros */
#define Z_DEFINE_STRICT_STRUCTURE_BEGIN  typedef struct __attribute__((packed)) {
#define Z_DEFINE_STRICT_STRUCTURE_END    }

/* 6502 hardware definitions — mirrors Z/hardware/CPU/architecture/6502.h */

#define Z_6502_ADDRESS_NMI_POINTER   0xFFFA
#define Z_6502_ADDRESS_RESET_POINTER 0xFFFC
#define Z_6502_ADDRESS_IRQ_POINTER   0xFFFE
#define Z_6502_ADDRESS_BRK_POINTER   0xFFFE
#define Z_6502_ADDRESS_STACK         0x0100

#define Z_6502_VALUE_AFTER_POWER_ON_PC 0x0000
#define Z_6502_VALUE_AFTER_POWER_ON_S  0xFD
#define Z_6502_VALUE_AFTER_POWER_ON_P  0x36
#define Z_6502_VALUE_AFTER_POWER_ON_A  0x00
#define Z_6502_VALUE_AFTER_POWER_ON_X  0x00
#define Z_6502_VALUE_AFTER_POWER_ON_Y  0x00

Z_DEFINE_STRICT_STRUCTURE_BEGIN
    zuint16 pc;
    zuint8  s, p, a, x, y;
    struct {
        zuint8 irq :1;
        zuint8 nmi :1;
    } internal;
Z_DEFINE_STRICT_STRUCTURE_END Z6502State;

#define Z_6502_STATE_PC( object) (object)->pc
#define Z_6502_STATE_S(  object) (object)->s
#define Z_6502_STATE_P(  object) (object)->p
#define Z_6502_STATE_A(  object) (object)->a
#define Z_6502_STATE_X(  object) (object)->x
#define Z_6502_STATE_Y(  object) (object)->y
#define Z_6502_STATE_NMI(object) (object)->internal.nmi
#define Z_6502_STATE_IRQ(object) (object)->internal.irq

#define Z_6502_STATE_MEMBER_PC  pc
#define Z_6502_STATE_MEMBER_S   s
#define Z_6502_STATE_MEMBER_P   p
#define Z_6502_STATE_MEMBER_A   a
#define Z_6502_STATE_MEMBER_X   x
#define Z_6502_STATE_MEMBER_Y   y
#define Z_6502_STATE_MEMBER_NMI internal.nmi
#define Z_6502_STATE_MEMBER_IRQ internal.irq
