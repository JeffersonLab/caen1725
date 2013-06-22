/******************************************************************************
 *
 * header file for use General USER defined rols with CODA crl (version 2.6.2)
 *    - For use with using CAEN1720 as a trigger source
 *
 *                             Bryan Moffit   June 2013
 *
 *******************************************************************************/
#ifndef __GEN_ROL__
#define __GEN_ROL__

static int GEN_handlers,GENflag;
static int GEN_isAsync;
static unsigned int *GENPollAddr = NULL;
static unsigned int GENPollMask;
static unsigned int GENPollValue;
static unsigned long GEN_prescale = 1;
static unsigned long GEN_count = 0;

#define GEN_LEVEL    5
#define GEN_VEC      0xe1

#ifdef VXWORKSPPC

#else
#include "jvme.h"
#include "remexLib.h"
#endif

/* Put any global user defined variables needed here for GEN readout */
#include "caen1720Lib.h"


void
GEN_int_handler()
{
  theIntHandler(GEN_handlers);                   /* Call our handler */
}



/*----------------------------------------------------------------------------
  gen_trigLib.c -- Dummy trigger routines for GENERAL USER based ROLs

  File : gen_trigLib.h

  Routines:
  void gentriglink();       link interrupt with trigger
  void gentenable();        enable trigger
  void gentdisable();       disable trigger
  char genttype();          return trigger type 
  int  genttest();          test for trigger  (POLL Routine)
  ------------------------------------------------------------------------------*/


static void
gentriglink(int code, VOIDFUNCPTR isr)
{

  /* Connect interrupt with defaults */
  c1720SetupInterrupt(0,GEN_LEVEL,GEN_VEC);


  /* Connect the ISR */
#ifdef VXWORKSPPC
  if(intDisconnect((int)(GEN_VEC)) != 0) 
    {
      printf("ERROR disconnecting Interrupt\n");
    }
  if(intConnect(GEN_VEC,isr,1) != 0) 
    {
      printf("ERROR in intConnect()\n");
    }
#else
  if(vmeIntDisconnect(GEN_LEVEL) != OK)
    {
      printf("ERROR disconnecting Interrupt\n");
    }
  if(vmeIntConnect(GEN_VEC,GEN_LEVEL,isr,1) != OK) 
    {
      printf("ERROR in intConnect()\n");
    }

#endif


}

static void 
gentenable(int code, int intMask)
{

  

#ifdef POLLING
  GENflag = 1;
#else
#ifdef VXWORKS
  sysIntEnable(GEN_LEVEL);
#endif
  c1720EnableInterrupts(0);
#endif
  c1720StartRun(0);
}

static void 
gentdisable(int code, int intMask)
{

  c1720StopRun(0);
#ifdef POLLING
  GENflag = 0;
#else
  c1720DisableInterrupts(0);
#endif
}

static void 
gentack(int code, int val)
{
  /* Need some sort of acknowledge here */
}


static unsigned long 
genttype(int code)
{
  return(1);
}

static int 
genttest(int code)
{
  unsigned int ret;

  if((GENflag>0) && (GENPollAddr > 0)) {
    GEN_count++;
    

    ret = c1720EventReady(0);;

    return ret;

  } else {
    return(0);
  }
}



/* Define CODA readout list specific Macro routines/definitions */

#define GEN_TEST  genttest

#define GEN_INIT { GEN_handlers =0;GEN_isAsync = 0;GENflag = 0;}

#define GEN_ASYNC(code,id)  {printf("linking async GEN trigger to id %d \n",id); \
    GEN_handlers = (id);GEN_isAsync = 1;gentriglink(code,GEN_int_handler);}

#define GEN_SYNC(code,id)   {printf("linking sync GEN trigger to id %d \n",id); \
    GEN_handlers = (id);GEN_isAsync = 0;}

#define GEN_SETA(code) GENflag = code;

#define GEN_SETS(code) GENflag = code;

#define GEN_ENA(code,val) gentenable(code, val);

#define GEN_DIS(code,val) gentdisable(code, val);

#define GEN_ACK(code,val) gentack(code,val);

#define GEN_CLRS(code) GENflag = 0;

#define GEN_GETID(code) GEN_handlers

#define GEN_TTYPE genttype

#define GEN_START(val)	 {;}

#define GEN_STOP(val)	 {;}

#define GEN_ENCODE(code) (code)


#endif

