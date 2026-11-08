/* MSVC's __ftol (0x4264e0), the original's float to integer conversion: the x87's FISTP into 64 bits
   with the rounding set to chop (toward zero), the low half returned; past the 64 bits' range the
   integer indefinite, whose low half is 0. (The rounding conversions are MUrFloat_Round 0x434030 and
   MUrUnsignedSmallFloat_To_Uns_Round 0x4340a0, to the nearest.) */
#ifndef OPENONI_FTOL_H
#define OPENONI_FTOL_H

#include <stdint.h>

static inline int32_t ftol(double v)
{
    if (!(v > -9.2233720368547758e18 && v < 9.2233720368547758e18)) return 0;
    return (int32_t)(uint32_t)(uint64_t)(int64_t)v;
}

#endif
