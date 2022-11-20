/**
 * @copyright Copyright 2022, Jefferson Science Associates, LLC.
 *            Subject to the terms in the LICENSE file found in the
 *            top-level directory.
 *
 * @author    Bryan Moffit
 *            moffit@jlab.org                   Jefferson Lab, MS-12B3
 *            Phone: (757) 269-5660             12000 Jefferson Ave.
 *            Fax:   (757) 269-5800             Newport News, VA 23606
 *
 * @author    Robert Michaels
 *            rom@jlab.org                      Jefferson Lab, MS-12B3
 *            Phone: (757) 269-7410             12000 Jefferson Ave.
 *                                              Newport News, VA 23606
 * @file      caen1725Lib.c
 * @brief     Library for the CAEN 1725 Digitizer - DPP-DAW Firmware
 *
 */


#ifdef VXWORKS
#include <vxWorks.h>
#include <logLib.h>
#include <taskLib.h>
#include <intLib.h>
#include <iv.h>
#include <semLib.h>
#include <vxLib.h>
#endif
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "jvme.h"
#include "caen1725Lib.h"

/* Mutex to guard TI read/writes */
pthread_mutex_t     c1725Mutex = PTHREAD_MUTEX_INITIALIZER;
#define C1725LOCK     if(pthread_mutex_lock(&c1725Mutex)<0) perror("pthread_mutex_lock");
#define C1725UNLOCK   if(pthread_mutex_unlock(&c1725Mutex)<0) perror("pthread_mutex_unlock");

/* Define external Functions */
#ifdef VXWORKS
IMPORT  STATUS sysBusToLocalAdrs (int, char *, char **);
#endif

/* Global variables */
int32_t Nc1725 = 0;      /* Number of FADCs in crate */
volatile c1725_address *c1725p[C1725_MAX_BOARDS];  /* pointers to memory map */
int32_t c1725ID[MAX_VME_SLOTS+1];                    /**< array of slot numbers */
static unsigned long c1725AddrOffset=0; /* offset between VME and local address */
static int32_t c1725IntLevel=5;        /* default interrupt level */
static int32_t c1725IntVector=0xa8;    /* default interrupt vector */

/* Some globals for test routines */
static int32_t def_acq_ctrl=0x1;       /* default acq_ctrl */
static int32_t def_dac_val=0x1000;     /* default DAC setting for each channel */

#define CHECKID(id)							\
  if((id<0) || (id>=MAX_VME_SLOTS) || (c1725p[id] == NULL))		\
    {									\
      printf("%s: ERROR : CAEN1725 id %d is not initialized \n",	\
	     __func__, id);						\
      return ERROR;							\
    }

int32_t
c1725CheckAddresses()
{
  int32_t rval = OK;
  uintptr_t offset = 0, expected = 0, base = 0;
  c1725_address fbase;

  printf("%s:\n\t ---------- Checking c1725 memory map ---------- \n",
	 __func__);

  base = (uintptr_t) &fbase;
  /** \cond PRIVATE */
#ifndef CHECKOFFSET
#define CHECKOFFSET(_x, _y)						\
  offset = ((uintptr_t) &fbase._y) - base;				\
  expected = _x;							\
  if(offset != expected)						\
    {									\
      printf("%s: ERROR ->%s not at offset = 0x%lx (@ 0x%lx)\n",	\
	     __func__, #_y ,expected, offset);				\
      rval = ERROR;							\
    }
#endif
  /** \endcond */

  CHECKOFFSET(0x1028, chan[0].input_dynamic_range);
  CHECKOFFSET(0x1128, chan[1].input_dynamic_range);
  CHECKOFFSET(0x1828, chan[8].input_dynamic_range);
  CHECKOFFSET(0x8000, config);
  CHECKOFFSET(0x809C, channel_adc_calibration);
  CHECKOFFSET(0x8120, channel_enable_mask);

  CHECKOFFSET(0x812C, event_stored);
  CHECKOFFSET(0xEF34, config_reload);

  return rval;
}


/*******************************************************************************
 *
 * c1725Init - Initialize CAEN 1725 Library.
 *
 *   ARGS:
 *       addr:  VME address of the first module.  This can be:
 *              <= 21        : Indicating the VME slot to use for CR-CSR addressing
 *                             (*** Not supported in vxWorks ***)
 *              < 0xFFFFFF   : Indicating the VME A24 address
 *              < 0xFFFFFFFF : Indicating the VME A32 address
 *
 *   addr_inc:  Incrementing address to initialize > 1 c1725
 *
 *       nadc:  Number of times to increment using addr_inc
 *
 *   RETURNS: OK, or ERROR if one or address increments resulted in ERROR
 *.
 */

int32_t
c1725Init(uint32_t addr, uint32_t addr_inc, int32_t nadc)
{

  int32_t i, res, errFlag=0;
  int32_t AMcode=0x39;
  int32_t boardInfo=0, boardID;
  unsigned long laddr;

  if(addr<=21) /* CR-CSR addressing */
    {
#ifdef VXWORKS
      printf("%s: ERROR: CR-CSR addressing not supported in vxWorks\n",
	     __FUNCTION__);
      return ERROR;
#else
      AMcode=0x2F;
      addr = addr<<19;
      addr_inc = addr_inc<<19;
      printf("%s: Initializing using CR-CSR (0x%02x)\n",__FUNCTION__,AMcode);
#endif
    }
  else if (addr < 0xffffff) /* A24 addressing */
    {
      AMcode=0x39;
      printf("%s: Initializing using A24 (0x%02x)\n",__FUNCTION__,AMcode);
    }
  else /* A32 addressing */
    {
      AMcode=0x09;
      printf("%s: Initializing using A32 (0x%02x)\n",__FUNCTION__,AMcode);
    }
#ifdef VXWORKS
  res = sysBusToLocalAdrs (AMcode, (char *) (unsigned long)addr, (char **) &laddr);
#else
  res = vmeBusToLocalAdrs (AMcode, (char *) (unsigned long)addr, (char **) &laddr);
#endif

  c1725AddrOffset = laddr-addr;

  if (res != 0)
    {
#ifdef VXWORKS
      printf ("%s: ERROR in sysBusToLocalAdrs (0x%02x, 0x%x, &laddr) \n",
	      __FUNCTION__,AMcode,addr);
#else
      printf ("%s: ERROR in vmeBusToLocalAdrs (0x%02x, 0x%x, &laddr) \n",
	      __FUNCTION__,AMcode,addr);
#endif
      return ERROR;
    }

  Nc1725 = 0;
  for (i = 0; i < nadc; i++)
    {
      c1725p[i] = (c1725_address *) (laddr + i * addr_inc);
      /* Check if Board exists at that address */
#ifdef VXWORKS
      res = vxMemProbe ((char *) &(c1725p[i]->board_info), VX_READ, 4, (char *) &boardInfo);
#else
      res = vmeMemProbe ((char *) &(c1725p[i]->board_info), 4, (char *) &boardInfo);
#endif

      if (res < 0)
	{
	  printf ("%s: ERROR: No addressable board at address = 0x%lx\n",
		  __FUNCTION__,
		  (unsigned long) c1725p[i] - c1725AddrOffset);
	  c1725p[i] = NULL;
	  errFlag = 1;
	  continue;
	}

      /* Check that this is a c1792 */
      boardID = (vmeRead32(&c1725p[i]->rom.board0) |
		 (vmeRead32(&c1725p[i]->rom.board1)<<8));

      if((boardID & C1725_BOARD_ID_MASK)!=C1725_BOARD_ID)
	{
	  printf("%s: Invalid board type (0x%x) at address 0x%lx\n",
		 __FUNCTION__,boardID, (unsigned long) c1725p[i] - c1725AddrOffset);
	  c1725p[i] = NULL;
	  errFlag = 1;
	  continue;
	}


      Nc1725++;
      printf ("%s: Initialized ADC ID %d at address 0x%08lx \n", __FUNCTION__,
	      i, (unsigned long) c1725p[i] - c1725AddrOffset);
    }

  if (errFlag > 0)
    {
      printf ("%s: ERROR: Unable to initialize all ADC Modules\n",__FUNCTION__);
      if (Nc1725 > 0)
	printf ("%s: %d ADC (s) successfully initialized\n", __FUNCTION__, Nc1725);

      return ERROR;
    }
  else
    {
      return OK;
    }

  return OK;
}

/**************************************************************************************
 *
 * c1725PrintChanStatus  - Print channel registers to standard out
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725PrintChanStatus(int32_t id, int32_t chan)
{
  uint32_t status=0, fpga_firmware=0, dac=0, thresh=0;

  CHECKID(id);
  if (chan < 0 || chan > 8) return ERROR;

  C1725LOCK;
  status           = vmeRead32(&c1725p[id]->chan[chan].status);
  fpga_firmware    = vmeRead32(&c1725p[id]->chan[chan].firmware_revision);
  dac              = vmeRead32(&c1725p[id]->chan[chan].dc_offset);
  thresh           = vmeRead32(&c1725p[id]->chan[chan].trigger_threshold);
  C1725UNLOCK;

  printf("Channel %d   status (0x1%d88) = 0x%x \n",chan,chan,status);
  printf("      firmware (0x1%d8c) = 0x%x\n",
	 chan, fpga_firmware);
  printf("     dac (0x1%d98) = 0x%x    threshold (0x1%d84) = 0x%x \n",
	 chan, dac,chan, thresh);

  return OK;
}


/**************************************************************************************
 *
 * c1725PrintStatus  - Print module status to standard out
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725PrintStatus(int32_t id)
{
  uint32_t firmware, board_info, config, buffer_org;
  uint32_t acq_ctrl, acq_status, relocation_address, readout_status;
  uint32_t board_id, interrupt_id;
  uint32_t global_trigger_mask;
  uint32_t c1725Base;
  int32_t winwidth=0, winpost=0;
  int32_t chan_print = 1;
  int32_t ichan;

  CHECKID(id);
  C1725LOCK;
  firmware     = vmeRead32(&c1725p[id]->roc_firmware_revision);
  board_info   = vmeRead32(&c1725p[id]->board_info);
  config  = vmeRead32(&c1725p[id]->config);
  buffer_org   = vmeRead32(&c1725p[id]->buffer_org);
  acq_ctrl     = vmeRead32(&c1725p[id]->acq_ctrl);
  acq_status   = vmeRead32(&c1725p[id]->acq_status);
  relocation_address   = vmeRead32(&c1725p[id]->relocation_address);
  readout_status   = vmeRead32(&c1725p[id]->readout_status);
  board_id     = vmeRead32(&c1725p[id]->board_id);
  interrupt_id = vmeRead32(&c1725p[id]->interrupt_id);
  global_trigger_mask = vmeRead32(&c1725p[id]->global_trigger_mask);
  C1725UNLOCK;

  c1725Base = (unsigned long)c1725p[id];

  printf("\nStatus for CAEN 1725 board %d \n",id);
  printf("--------------------------------------------------------------------------------\n");
  printf("Firmware           (0x%04lx) = 0x%08x\n",
	 (unsigned long)(&c1725p[id]->roc_firmware_revision)-c1725Base,firmware);
  printf("Board info         (0x%04lx) = 0x%08x\n",
	 (unsigned long)(&c1725p[id]->board_info)-c1725Base,board_info);
  printf("Chan config        (0x%04lx) = 0x%08x\n",
	 (unsigned long)(&c1725p[id]->config)-c1725Base,config);
  printf("Buffer org         (0x%04lx) = 0x%08x\n",
	 (unsigned long)(&c1725p[id]->buffer_org)-c1725Base,buffer_org);
  printf("Acq control        (0x%04lx) = 0x%08x\n",
	 (unsigned long)(&c1725p[id]->acq_ctrl)-c1725Base,acq_ctrl);
  printf("Acq status         (0x%04lx) = 0x%08x\n",
	 (unsigned long)(&c1725p[id]->acq_status)-c1725Base,acq_status);
  printf("Relocation address (0x%04lx) = 0x%08x\n",
	 (unsigned long)(&c1725p[id]->relocation_address)-c1725Base,relocation_address);
  printf("VME Status         (0x%04lx) = 0x%08x\n",
	 (unsigned long)(&c1725p[id]->readout_status)-c1725Base,readout_status);
  printf("Board id           (0x%04lx) = 0x%08x\n",
	 (unsigned long)(&c1725p[id]->board_id)-c1725Base,board_id);
  printf("Interrupt id       (0x%04lx) = 0x%08x\n",
	 (unsigned long)(&c1725p[id]->interrupt_id)-c1725Base,interrupt_id);
  printf("TrigSrc Mask       (0x%04lx) = 0x%08x\n",
	 (unsigned long)(&c1725p[id]->global_trigger_mask)-c1725Base,global_trigger_mask);
  printf("\n");

  printf("ROC FPGA Firmware version: %d.%d\n",(firmware&0xFF00)>>8, firmware&0xFF);
  printf("Channel Configuration: \n");
  printf(" - Trigger Overlapping: %s\n",
	 (config&C1725_CHAN_CONFIG_TRIG_OVERLAP) ? "on" : "off");
  printf(" - Trigger for %s threshold\n",
	 (config&C1725_CHAN_CONFIG_TRIGOUT_UNDER_THRESHOLD) ? "UNDER" : "OVER");
  printf(" - Pack2.5 Encoding: %s\n",
	 (config&C1725_CHAN_CONFIG_PACK2_5) ? "on" : "off");
  if(config&C1725_CHAN_CONFIG_ZLE)
    printf(" - Zero Length Encoding: on\n");
  if(config&C1725_CHAN_CONFIG_ZS_AMP)
    printf(" - Amplitude based full suppression encoding: on\n");

  printf("\n\n");
  if (chan_print)
    {
      for (ichan = 0; ichan < 8; ichan++)
	{
	  c1725PrintChanStatus(id,ichan);
	}
    }

  printf("--------------------------------------------------------------------------------\n");

  return OK;

}

/**************************************************************************************
 *
 * c1725Reset  - reset the board -- clear output buffer, event counter,
 *      and performs a FPGAs global reset to restore FPGAs to
 *      their default config.  Also initializes counters to
 *      their initial state and clears all error conditions.
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725Reset(int32_t id)
{
  CHECKID(id);
  C1725LOCK;
  vmeWrite32(&c1725p[id]->software_reset, 1);
  vmeWrite32(&c1725p[id]->readout_ctrl, 0x10);
  vmeWrite32(&c1725p[id]->channel_enable_mask, 0xff);
  C1725UNLOCK;

  return OK;

}

/**************************************************************************************
 *
 * c1725Clear  - Clear the output buffer
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725Clear(int32_t id)
{
  CHECKID(id);
  C1725LOCK;
  vmeWrite32(&c1725p[id]->software_clear, 1);
  C1725UNLOCK;
  c1725SetAcqCtrl(id, 0);

  return OK;

}

/**************************************************************************************
 *
 * c1725SoftTrigger  - Generate a software trigger.  Software trigger must be
 *     enabled (with c1725EnableTriggerSource(...))
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725SoftTrigger(int32_t id)
{

  CHECKID(id);
  C1725LOCK;
  vmeWrite32(&c1725p[id]->sw_trigger, 1);
  C1725UNLOCK;

  return OK;

}

/**************************************************************************************
 *
 * c1725SetTriggerOverlapping  - Enable/Disable trigger overlapping feature
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725SetTriggerOverlapping(int32_t id, int32_t enable)
{
  CHECKID(id);
  C1725LOCK;
  if(enable)
    vmeWrite32(&c1725p[id]->config_bitset, C1725_CHAN_CONFIG_TRIG_OVERLAP);
  else
    vmeWrite32(&c1725p[id]->config_bitclear, C1725_CHAN_CONFIG_TRIG_OVERLAP);
  C1725UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1725SetTestPatternGeneration  - Enable/Disable Test Pattern Generation
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725SetTestPatternGeneration(int32_t id, int32_t enable)
{
  CHECKID(id);
  C1725LOCK;
  if(enable)
    vmeWrite32(&c1725p[id]->config_bitset, C1725_CHAN_CONFIG_TEST_PATTERN);
  else
    vmeWrite32(&c1725p[id]->config_bitclear, C1725_CHAN_CONFIG_TEST_PATTERN);
  C1725UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1725SetTriggerOnUnderThreshold  - Enable/Disable triggering on "under" threshold
 *         (as opposed to "over" threshold)
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725SetTriggerOnUnderThreshold(int32_t id, int32_t enable)
{
  CHECKID(id);
  C1725LOCK;
  if(enable)
    vmeWrite32(&c1725p[id]->config_bitset, C1725_CHAN_CONFIG_TRIGOUT_UNDER_THRESHOLD);
  else
    vmeWrite32(&c1725p[id]->config_bitclear, C1725_CHAN_CONFIG_TRIGOUT_UNDER_THRESHOLD);
  C1725UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1725SetPack2_5  - Enable/Disable Pack2.5 data encoding (2.5 samples per 32bit
 *       data word)
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725SetPack2_5(int32_t id, int32_t enable)
{
  CHECKID(id);
  C1725LOCK;
  if(enable)
    vmeWrite32(&c1725p[id]->config_bitset, C1725_CHAN_CONFIG_PACK2_5);
  else
    vmeWrite32(&c1725p[id]->config_bitclear, C1725_CHAN_CONFIG_PACK2_5);
  C1725UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1725SetZeroLengthEncoding  - Enable/Disable Zero Length Encoding (ZLE).
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725SetZeroLengthEncoding(int32_t id, int32_t enable)
{
  CHECKID(id);
  C1725LOCK;
  if(enable)
    vmeWrite32(&c1725p[id]->config_bitset, C1725_CHAN_CONFIG_ZLE);
  else
    vmeWrite32(&c1725p[id]->config_bitclear, C1725_CHAN_CONFIG_ZLE);
  C1725UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1725SetAmplitudeBasedFullSuppression  - Enable/Disable Full Suppression based
 *     on the amplitude of the signal.
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725SetAmplitudeBasedFullSuppression(int32_t id, int32_t enable)
{
  CHECKID(id);
  C1725LOCK;
  if(enable)
    vmeWrite32(&c1725p[id]->config_bitset, C1725_CHAN_CONFIG_ZS_AMP);
  else
    vmeWrite32(&c1725p[id]->config_bitclear, C1725_CHAN_CONFIG_ZS_AMP);
  C1725UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1725EnableTriggerSource  - Enable a trigger source
 *    Args:
 *         src: Integer indicating the trigger source to enable
 *              0: Software
 *              1: External (Front Panel)
 *              2: Channel (Internal)
 *              3: All of the above
 *
 *    chanmask: Bit mask of channels to include in internal trigger logic
 *              (used for src=2,3)
 *
 *       level: Coincidence level of the channels include in chanmask
 *              (used for src=2,3)
 *              Note: Coincidence level must be smaller than the number
 *                    of channels enabled via chanmask
 *
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725EnableTriggerSource(int32_t id, int32_t src, int32_t chanmask, int32_t level)
{
  int32_t enablebits=0, prevbits=0;
  int32_t setlevel=0;
  CHECKID(id);
  switch(src)
    {
    case C1725_SOFTWARE_TRIGGER_ENABLE:
      {
	enablebits = C1725_TRIGMASK_ENABLE_SOFTWARE;
	printf("%s: Enabling Software triggers\n",__FUNCTION__);
	break;
      }

    case C1725_EXTERNAL_TRIGGER_ENABLE:
      {
	enablebits = C1725_TRIGMASK_ENABLE_EXTERNAL;
	printf("%s: Enabling External triggers\n",__FUNCTION__);
	break;
      }

    case C1725_CHANNEL_TRIGGER_ENABLE:
      {
	if(chanmask > C1725_TRIGMASK_ENABLE_CHANNEL_MASK)
	  {
	    printf("%s: ERROR: Invalid channel mask (0x%x)\n",
		   __FUNCTION__,chanmask);
	    return ERROR;
	  }
	if(level > 7)
	  {
	    printf("%s: ERROR: Invalid coincidence level (%d)\n",
		   __FUNCTION__,level);
	    return ERROR;
	  }
	enablebits = chanmask;
	enablebits |= (level<<24);
	setlevel=1;
	printf("%s: Enabling Channel triggers (mask=0x%02x, coincidence level = %d)\n",
	       __FUNCTION__,chanmask,level);

	break;
      }

    case C1725_ALL_TRIGGER_ENABLE:
    default:
      {
	if(chanmask > C1725_TRIGMASK_ENABLE_CHANNEL_MASK)
	  {
	    printf("%s: ERROR: Invalid channel mask (0x%x)\n",
		   __FUNCTION__,chanmask);
	    return ERROR;
	  }
	if(level > 7)
	  {
	    printf("%s: ERROR: Invalid coincidence level (%d)\n",
		   __FUNCTION__,level);
	    return ERROR;
	  }

	enablebits  = C1725_TRIGMASK_ENABLE_SOFTWARE;
	enablebits |= C1725_TRIGMASK_ENABLE_EXTERNAL;
	enablebits |= chanmask;
	enablebits |= (level<<24);
	setlevel=1;
	printf("%s: Enabling Software, External, and Channel triggers\n",__FUNCTION__);
	printf("\t(mask=0x%02x, coincidence level = %d)\n",chanmask,level);
      }

    } /* switch(src) */


  C1725LOCK;
  prevbits = vmeRead32(&c1725p[id]->global_trigger_mask);

  if(setlevel)
    { /* enablebits contains a new coincidence level */
      enablebits = (prevbits & ~C1725_TRIGMASK_ENABLE_COINC_LEVEL_MASK) | enablebits;
    }
  else
    { /* leave coincidence level unchanged */
      enablebits = (prevbits | enablebits);
    }

  vmeWrite32(&c1725p[id]->global_trigger_mask, enablebits);
  C1725UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1725DisableTriggerSource  - Disable a trigger source
 *    Args:
 *         src: Integer indicating the trigger source to disable
 *              0: Software
 *              1: External (Front Panel)
 *              2: Channel (Internal)
 *              3: All of the above
 *
 *    chanmask: Bit mask of channels to exclude in internal trigger logic
 *              (used for src=2,3)
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725DisableTriggerSource(int32_t id, int32_t src, int32_t chanmask)
{
  uint32_t disablebits=0;
  CHECKID(id);

  switch(src)
    {
    case C1725_SOFTWARE_TRIGGER_ENABLE:
      {
	disablebits = C1725_TRIGMASK_ENABLE_SOFTWARE;
	printf("%s: Disabling Software triggers\n",__FUNCTION__);
	break;
      }

    case C1725_EXTERNAL_TRIGGER_ENABLE:
      {
	disablebits = C1725_TRIGMASK_ENABLE_EXTERNAL;
	printf("%s: Disabling External triggers\n",__FUNCTION__);
	break;
      }

    case C1725_CHANNEL_TRIGGER_ENABLE:
      {
	if(chanmask > C1725_TRIGMASK_ENABLE_CHANNEL_MASK)
	  {
	    printf("%s: ERROR: Invalid channel mask (0x%x)\n",
		   __FUNCTION__,chanmask);
	    return ERROR;
	  }

	disablebits = chanmask;
	printf("%s: Disabling Channel triggers (mask=0x%02x)\n",
	       __FUNCTION__,chanmask);

	break;
      }

    case C1725_ALL_TRIGGER_ENABLE:
    default:
      {
	if(chanmask > C1725_TRIGMASK_ENABLE_CHANNEL_MASK)
	  {
	    printf("%s: ERROR: Invalid channel mask (0x%x)\n",
		   __FUNCTION__,chanmask);
	    return ERROR;
	  }

	disablebits  = C1725_TRIGMASK_ENABLE_SOFTWARE;
	disablebits |= C1725_TRIGMASK_ENABLE_EXTERNAL;
	disablebits |= chanmask;

	printf("%s: Disabling Software, External, and Channel triggers\n",__FUNCTION__);
	printf("\t(mask=0x%02x)\n",chanmask);
      }

    } /* switch(src) */

  C1725LOCK;
  vmeWrite32(&c1725p[id]->global_trigger_mask,
	     vmeRead32(&c1725p[id]->global_trigger_mask) & ~disablebits);
  C1725UNLOCK;


  return OK;
}

/**************************************************************************************
 *
 * c1725EnableFPTrigOut  - Enable the trigger output on the front panel from
 *     specified trigger source.
 *    Args:
 *         src: Integer indicating the trigger source to contribute
 *              0: Software
 *              1: External (Front Panel)
 *              2: Channel (Internal)
 *              3: All of the above
 *
 *    chanmask: Bit mask of channels to include
 *              (used for src=2,3)
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725EnableFPTrigOut(int32_t id, int32_t src, int32_t chanmask)
{
  int32_t enablebits=0;
  CHECKID(id);
  switch(src)
    {
    case C1725_SOFTWARE_TRIGGER_ENABLE:
      {
	enablebits = C1725_TRIGMASK_ENABLE_SOFTWARE;
	break;
      }

    case C1725_EXTERNAL_TRIGGER_ENABLE:
      {
	enablebits = C1725_TRIGMASK_ENABLE_EXTERNAL;
	break;
      }

    case C1725_CHANNEL_TRIGGER_ENABLE:
      {
	if(chanmask > C1725_TRIGMASK_ENABLE_CHANNEL_MASK)
	  {
	    printf("%s: ERROR: Invalid channel mask (0x%x)\n",
		   __FUNCTION__,chanmask);
	    return ERROR;
	  }

	enablebits = chanmask;
	break;
      }

    case C1725_ALL_TRIGGER_ENABLE:
    default:
      {
	if(chanmask > C1725_TRIGMASK_ENABLE_CHANNEL_MASK)
	  {
	    printf("%s: ERROR: Invalid channel mask (0x%x)\n",
		   __FUNCTION__,chanmask);
	    return ERROR;
	  }

	enablebits  = C1725_TRIGMASK_ENABLE_SOFTWARE;
	enablebits |= C1725_TRIGMASK_ENABLE_EXTERNAL;
	enablebits |= chanmask;
      }

    } /* switch(src) */


  C1725LOCK;
  vmeWrite32(&c1725p[id]->fp_trg_out_enable_mask,
	     vmeRead32(&c1725p[id]->fp_trg_out_enable_mask) | enablebits);
  C1725UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 *  c1725DisableFPTrigOut - Disable a trigger signal contributing to the trigger
 *      output on the front panel.
 *
 *    Args:
 *         src: Integer indicating the trigger source to disable
 *              0: Software
 *              1: External (Front Panel)
 *              2: Channel (Internal)
 *              3: All of the above
 *
 *    chanmask: Bit mask of channels to exclude
 *              (used for src=2,3)
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725DisableFPTrigOut(int32_t id, int32_t src, int32_t chanmask)
{
  uint32_t disablebits=0;
  CHECKID(id);

  switch(src)
    {
    case C1725_SOFTWARE_TRIGGER_ENABLE:
      {
	disablebits = C1725_TRIGMASK_ENABLE_SOFTWARE;
	break;
      }

    case C1725_EXTERNAL_TRIGGER_ENABLE:
      {
	disablebits = C1725_TRIGMASK_ENABLE_EXTERNAL;
	break;
      }

    case C1725_CHANNEL_TRIGGER_ENABLE:
      {
	if(chanmask > C1725_TRIGMASK_ENABLE_CHANNEL_MASK)
	  {
	    printf("%s: ERROR: Invalid channel mask (0x%x)\n",
		   __FUNCTION__,chanmask);
	    return ERROR;
	  }

	disablebits = chanmask;
	break;
      }

    case C1725_ALL_TRIGGER_ENABLE:
    default:
      {
	if(chanmask > C1725_TRIGMASK_ENABLE_CHANNEL_MASK)
	  {
	    printf("%s: ERROR: Invalid channel mask (0x%x)\n",
		   __FUNCTION__,chanmask);
	    return ERROR;
	  }

	disablebits  = C1725_TRIGMASK_ENABLE_SOFTWARE;
	disablebits |= C1725_TRIGMASK_ENABLE_EXTERNAL;
	disablebits |= chanmask;
      }

    } /* switch(src) */

  C1725LOCK;
  vmeWrite32(&c1725p[id]->fp_trg_out_enable_mask,
	     vmeRead32(&c1725p[id]->fp_trg_out_enable_mask) & ~disablebits);
  C1725UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1725SetEnableChannelMask  - Set which channels provide the samples which are
 *     stored into the events.
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725SetEnableChannelMask(int32_t id, int32_t chanmask)
{
  CHECKID(id);

  if(chanmask>C1725_ENABLE_CHANNEL_MASK)
    {
      printf("%s: ERROR: Invalid channel mask (0x%x)\n",
	     __FUNCTION__,chanmask);
      return ERROR;
    }

  C1725LOCK;
  vmeWrite32(&c1725p[id]->channel_enable_mask,chanmask);
  C1725UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1725GetEventSize  - Obtain the number of 32bit words in the next event.
 *
 * RETURNS: Event Size if successful, ERROR otherwise.
 *
 */

uint32_t
c1725GetEventSize(int32_t id)
{
  uint32_t rval=0;

  CHECKID(id);

  C1725LOCK;
  rval = vmeRead32(&c1725p[id]->event_size);
  C1725UNLOCK;

  return rval;
}


/**************************************************************************************
 *
 * c1725GetNumEv  - Obtain the number of events current stored in the output buffer
 *
 * RETURNS: Number of events if successful, ERROR otherwise.
 *
 */

uint32_t
c1725GetNumEv(int32_t id)
{
  uint32_t rval=0;

  CHECKID(id);

  C1725LOCK;
  rval = vmeRead32(&c1725p[id]->event_stored);
  C1725UNLOCK;

  return rval;

}

/**************************************************************************************
 *
 * c1725SetChannelDAC  - Set the DC offset to be added to the input signal.
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725SetChannelDAC(int32_t id, int32_t chan, int32_t dac)
{
  int32_t iwait=0, maxwait=1000;
  CHECKID(id);  if (chan < 0 || chan > 8) return ERROR;

  printf("%s: Writing DAC for id=%d  chan=%d   value=%d\n",
	 __FUNCTION__,id,chan,dac);

  C1725LOCK;
  vmeWrite32(&c1725p[id]->chan[chan].dc_offset, dac);
  while(iwait<maxwait)
    {
      if((vmeRead32(&c1725p[id]->chan[chan].status) & C1725_CHANNEL_STATUS_BUSY)==0)
	break;
      iwait++;
    }
  C1725UNLOCK;
  if(iwait>=maxwait)
    {
      printf("%s: ERROR: Timeout in setting the DAC\n",__FUNCTION__);
      return ERROR;
    }

  return OK;

}

/**************************************************************************************
 *
 * c1725SetAcqCtrl  - Set the acquisition control register
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725SetAcqCtrl(int32_t id, int32_t bits)
{

  uint32_t acq;

  CHECKID(id);
  C1725LOCK;
  acq = vmeRead32(&c1725p[id]->acq_ctrl);
  vmeWrite32(&c1725p[id]->acq_ctrl, (acq | bits));
  C1725UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1725BoardReady  - Determine if the module is ready for acquisition.
 *
 * RETURNS: 1 if ready, 0 if not ready, ERROR otherwise.
 *
 */

int32_t
c1725BoardReady(int32_t id)
{
  uint32_t rval=0;

  CHECKID(id);
  C1725LOCK;
  rval = (vmeRead32(&c1725p[id]->acq_status) & C1725_ACQ_STATUS_ACQ_READY)>>8;
  C1725UNLOCK;

  return rval;
}

/**************************************************************************************
 *
 * c1725EventReady  - Determine if at least one event is ready for readout
 *
 * RETURNS: 1 if data is ready, 0 if not, ERROR otherwise.
 *
 */

int32_t
c1725EventReady(int32_t id)
{

  uint32_t status1=0, status2=0;
  CHECKID(id);
  C1725LOCK;
  status1 = (vmeRead32(&c1725p[id]->acq_status) & C1725_ACQ_STATUS_EVENT_READY);
  status2 = (vmeRead32(&c1725p[id]->readout_status) & C1725_VME_STATUS_EVENT_READY);
  C1725UNLOCK;

  if (status1 && status2)
    return 1;

  return 0;
}

/**************************************************************************************
 *
 * c1725SetBufOrg  - Set the organization of blocks in the output buffer memory
 *     See Manual for code to memory division translation table.
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725SetBufOrg(int32_t id, int32_t code)
{

  CHECKID(id);
  C1725LOCK;
  vmeWrite32(&c1725p[id]->buffer_org, code);
  C1725UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1725SetBusError  - Enable/Disable Bus Error termination for block transfers.
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725SetBusError(int32_t id, int32_t enable)
{
  CHECKID(id);
  C1725LOCK;
  if(enable)
    {
      vmeWrite32(&c1725p[id]->readout_ctrl,
		 vmeRead32(&c1725p[id]->readout_ctrl) | C1725_VME_CTRL_BERR_ENABLE);
    }
  else
    {
      vmeWrite32(&c1725p[id]->readout_ctrl,
		 vmeRead32(&c1725p[id]->readout_ctrl) & ~C1725_VME_CTRL_BERR_ENABLE);
    }
  C1725UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1725SetAlign64  - Enable/disable 64bit alignment for data words in a block transfer
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725SetAlign64(int32_t id, int32_t enable)
{
  CHECKID(id);
  C1725LOCK;
  if(enable)
    {
      vmeWrite32(&c1725p[id]->readout_ctrl,
		 vmeRead32(&c1725p[id]->readout_ctrl) | C1725_VME_CTRL_ALIGN64_ENABLE);
    }
  else
    {
      vmeWrite32(&c1725p[id]->readout_ctrl,
		 vmeRead32(&c1725p[id]->readout_ctrl) & ~C1725_VME_CTRL_ALIGN64_ENABLE);
    }
  C1725UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1725SetChannelThreshold  - Set the channel threshold used for trigger and/or
 *     data suppression.
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725SetChannelThreshold(int32_t id, int32_t chan, int32_t thresh)
{
  CHECKID(id);
  if((chan<0) || (chan>7))
    {
      printf("%s: ERROR: Invalid channel (%d)\n",
	     __FUNCTION__,chan);
      return ERROR;
    }

  if(thresh>C1725_CHANNEL_THRESHOLD_MASK)
    {
      printf("%s: ERROR: Invalid threshold (%d)\n",
	     __FUNCTION__,thresh);
      return ERROR;
    }

  C1725LOCK;
  vmeWrite32(&c1725p[id]->chan[chan].trigger_threshold, thresh);
  C1725UNLOCK;

  return OK;
}



/**************************************************************************************
 *
 * c1725SetMonitorMode  - Set the mode of the front panel monitor output
 *
 *   ARGs:
 *           mode:
 *                0: Trigger Majority
 *                1: Test
 *                3: Buffer Occupancy
 *                4: Voltage Level
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725SetMonitorMode(int32_t id, int32_t mode)
{
  CHECKID(id);
  if((mode>4) || (mode==2))
    {
      printf("%s: ERROR: Invalid mode (%d)\n",
	     __FUNCTION__,mode);
      return ERROR;
    }

  C1725LOCK;
  vmeWrite32(&c1725p[id]->analog_monitor_mode, mode);
  C1725UNLOCK;

  return OK;
}


/**************************************************************************************
 *
 * c1725SetMonitorDAC  - Set the DAC value for the front panel monitor output
 *           -- Relevant for Monitor Mode 4
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725SetMonitorDAC(int32_t id, int32_t dac)
{
  CHECKID(id);
  if(dac>C1725_MONITOR_DAC_MASK)
    {
      printf("%s: ERROR: Invalid dac (%d)\n",
	     __FUNCTION__,dac);
      return ERROR;
    }

  C1725LOCK;
  vmeWrite32(&c1725p[id]->voltage_level_mode_config, dac);
  C1725UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1725SetupInterrupt  - Set the interrupt level and vector.
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725SetupInterrupt(int32_t id, int32_t level, int32_t vector)
{
  CHECKID(id);
  if(level==0)
    {
      printf("%s: ERROR: Invalid interrupt level (%d)\n",
	     __FUNCTION__,level);
      return ERROR;
    }

  C1725LOCK;
  vmeWrite32(&c1725p[id]->interrupt_id,vector);
  c1725IntVector = vector;
  c1725IntLevel = level;
  C1725UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1725EnableInterrupts  - Enable interrupt generation on trigger
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725EnableInterrupts(int32_t id)
{
  CHECKID(id);
  C1725LOCK;
  vmeWrite32(&c1725p[id]->readout_ctrl,
	     (vmeRead32(&c1725p[id]->readout_ctrl) &~C1725_VME_CTRL_INTLEVEL_MASK)
	     | c1725IntLevel);
  C1725UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1725DisableInterrupts  - Disable interrupt generation
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725DisableInterrupts(int32_t id)
{
  CHECKID(id);
  C1725LOCK;
  vmeWrite32(&c1725p[id]->readout_ctrl,
	     (vmeRead32(&c1725p[id]->readout_ctrl) &~C1725_VME_CTRL_INTLEVEL_MASK));
  C1725UNLOCK;

  return OK;

}

/**************************************************************************************
 *
 *  c1725ReadEvent - General Data readout routine
 *
 *    id    - ID number of module to read
 *    data  - local memory address to place data
 *    nwrds - Max number of words to transfer
 *    rflag - Readout Flag
 *              0 - programmed I/O from the specified board
 *              1 - DMA transfer using Universe/Tempe DMA Engine
 *                    (DMA VME transfer Mode must be setup prior)
 */

int32_t
c1725ReadEvent(int32_t id, volatile uint32_t *data, int32_t nwrds, int32_t rflag)
{
  int32_t dCnt=0;
  uint32_t tmpData=0, evLen=0;
  CHECKID(id);
  if(data==NULL)
    {
      logMsg("c1725ReadEvent: ERROR: Invalid Destination address\n",0,0,0,0,0,0);
      return ERROR;
    }

  C1725LOCK;
  if(rflag==0)
    { /* Programmed I/O */
      /* First word should be the header */
      tmpData = vmeRead32(&c1725p[id]->readout_buffer[0]);
      if( (tmpData & C1725_HEADER_TYPE_MASK) != 0xA0000000)
	{
	  logMsg("c1725ReadEvent: ERROR: Invalid Header Word (0x%08x) for id = %d\n",
		 tmpData,id,3,4,5,6);
	  C1725UNLOCK;
	  return ERROR;
	}

      evLen = tmpData & C1725_HEADER_EVENTSIZE_MASK;
#ifdef VXWORKS
      data[dCnt++] = tmpData;
#else
      data[dCnt++] = LSWAP(tmpData);
#endif
      while(dCnt<evLen)
	{
	  /* Do not byteswap here (Linux), to make it consistent with DMA */
	  data[dCnt] = c1725p[id]->readout_buffer[0];;
	  dCnt++;
	  if(dCnt>=nwrds)
	    {
	      logMsg("c1725ReadEvent: WARN: Transfer limit reached.  nwrds = %d, evLen = %d, dCnt = %d\n",
		     nwrds, evLen, dCnt,4,5,6);
	      C1725UNLOCK;
	      return dCnt;
	    }
	}

      C1725UNLOCK;
      return dCnt;
    } /* rflag == 0 */
  else /* rflag == ? */
    {
      logMsg("c1725ReadEvent: ERROR: Unsupported readout flag (%d)\n",
	     rflag,2,3,4,5,6);
      C1725UNLOCK;
      return ERROR;
    }

  return OK;
}
