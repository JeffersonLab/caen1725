/************************************************************
 * vxCompat.h  - Header for compatibility routines
 *
 *   - Routines to help with compatibility with Linux
 *     libraries
 *
 */

#ifndef __VXCOMPAT_H__
#define __VXCOMPAT_H__

/* Register Read/Write routines */
unsigned char
vmeRead8(volatile unsigned char *addr)
{
  unsigned char rval;

  rval = *addr;

  return rval;
}

unsigned short
vmeRead16(volatile unsigned short *addr)
{
  unsigned short rval;

  rval = *addr;

  return rval;
}

unsigned int
vmeRead32(volatile unsigned int *addr)
{
  unsigned int rval;

  rval = *addr;

  return rval;
}

void
vmeWrite8(volatile unsigned char *addr, unsigned char val)
{

  *addr = val;

  return;
}

void
vmeWrite16(volatile unsigned short *addr, unsigned short val)
{

  *addr = val;

  return;
}

void
vmeWrite32(volatile unsigned int *addr, unsigned int val)
{

  *addr = val;

  return;
}

#endif /*  __VXCOMPAT_H__ */
