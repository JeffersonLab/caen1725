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
 * @brief     Library for the CAEN 1725 Digitizer
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
static unsigned long c1725AddrOffset=0; /* offset between VME and local address */
static int32_t c1725IntLevel=5;        /* default interrupt level */
static int32_t c1725IntVector=0xa8;    /* default interrupt vector */

/* Some globals for test routines */
static int32_t def_acq_ctrl=0x1;       /* default acq_ctrl */
static int32_t def_dac_val=0x1000;     /* default DAC setting for each channel */


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
      boardID = ((vmeRead32(&c1725p[i]->rom.board0)<<16) |
		 (vmeRead32(&c1725p[i]->rom.board1)<<8) |
		 vmeRead32(&c1725p[i]->rom.board2));
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


inline int
c1725Check(int32_t id, const char *func)
{

  if (!Nc1725 || id >= Nc1725)
    {
      printf("%s: ERROR: Board %d not initialized \n",func,id);
      return ERROR;
    }

  if(c1725p[id]==NULL)
    {
      printf("%s: ERROR: Invalid pointer for board %d \n",func,id);
      return ERROR;
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

  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;
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
  uint32_t firmware, board_info, config, buffer_org, custom_size;
  uint32_t acq_ctrl, acq_status, relocation_address, readout_status;
  uint32_t board_id, interrupt_id;
  uint32_t global_trigger_mask, post_trigger;
  uint32_t c1725Base;
  int32_t winwidth=0, winpost=0;
  int32_t chan_print = 1;
  int32_t ichan;

  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1725LOCK;
  firmware     = vmeRead32(&c1725p[id]->roc_firmware_revision);
  board_info   = vmeRead32(&c1725p[id]->board_info);
  config  = vmeRead32(&c1725p[id]->config);
  buffer_org   = vmeRead32(&c1725p[id]->buffer_org);
  custom_size  = vmeRead32(&c1725p[id]->custom_size);
  acq_ctrl     = vmeRead32(&c1725p[id]->acq_ctrl);
  acq_status   = vmeRead32(&c1725p[id]->acq_status);
  relocation_address   = vmeRead32(&c1725p[id]->relocation_address);
  readout_status   = vmeRead32(&c1725p[id]->readout_status);
  board_id     = vmeRead32(&c1725p[id]->board_id);
  interrupt_id = vmeRead32(&c1725p[id]->interrupt_id);
  global_trigger_mask = vmeRead32(&c1725p[id]->global_trigger_mask);
  post_trigger = vmeRead32(&c1725p[id]->post_trigger);
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
  printf("Buffer size (cust) (0x%04lx) = 0x%08x\n",
	 (unsigned long)(&c1725p[id]->custom_size)-c1725Base,custom_size);
  printf("Post trig          (0x%04lx) = 0x%08x\n",
	 (unsigned long)(&c1725p[id]->post_trigger)-c1725Base,post_trigger);
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
  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

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
  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

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

  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

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
  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

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
  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

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
  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

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
  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

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
  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

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
  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

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
  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

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
  if (c1725Check(id,__FUNCTION__)==ERROR) return 0;

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
  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

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
  if (c1725Check(id,__FUNCTION__)==ERROR) return 0;

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
  if (c1725Check(id,__FUNCTION__)==ERROR) return 0;

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

  if (c1725Check(id,__FUNCTION__)==ERROR) return 0;

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

  if (c1725Check(id,__FUNCTION__)==ERROR) return 0;

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
  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;
  if (chan < 0 || chan > 8) return ERROR;

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

  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

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

  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

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
  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

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

  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1725LOCK;
  vmeWrite32(&c1725p[id]->buffer_org, code);
  C1725UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1725SetBufferSize  - Set the custom buffer size
 *       This equates to the total number of 32bit data words per channel.
 *       How this translates to number of samples, depends on the encoding.
 *       For normal encoding, val=1 -> 2 samples
 *           Pack2.5          val=1 -> 2.5 samples
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725SetBufferSize(int32_t id, int32_t val)
{

  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1725LOCK;
  vmeWrite32(&c1725p[id]->custom_size, val);
  C1725UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1725SetPostTrig  - Set the Post Trigger Setting register
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int32_t
c1725SetPostTrig(int32_t id, int32_t val)
{

  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1725LOCK;
  vmeWrite32(&c1725p[id]->post_trigger, val);
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
  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

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
  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

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
  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

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
  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

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
  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

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
  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

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
  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

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
  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

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
  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

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

/* Start of some test code */

int32_t
c1725DefaultSetup(int32_t id)
{

  int32_t loop, maxloop, chan;
  maxloop = 10000;

  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

  c1725Reset(id);

  loop=0;
  while (loop++ < maxloop)
    {
      if (c1725BoardReady(id)) break;
    }

  c1725Clear(id);
  c1725StopRun(id);

  c1725SetBufOrg(id, 4);  /* #buffers = 2^N */
  c1725SetPostTrig(id, 40);

  c1725SetAcqCtrl(id, def_acq_ctrl);

  /* Following two are defaults after reset anyway (i.e. not necessary
     to set them here) */
  /*  c1725p[id]->global_trigger_mask = 0xc0000000;
      c1725p[id]->channel_enable_mask = 0xff; */

  C1725LOCK;
  vmeWrite32(&c1725p[id]->config, 0x10);
  C1725UNLOCK;

  for (chan=0; chan<8; chan++)
    {
      c1725SetChannelDAC(id, chan, def_dac_val);
    }

  return OK;

}

int32_t
c1725StartRun(int32_t id)
{

  int32_t acq;

  printf("\nc1725: Starting a run \n");

  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1725LOCK;
  acq = vmeRead32(&c1725p[id]->acq_ctrl);
  vmeWrite32(&c1725p[id]->acq_ctrl, (acq | 0x4));
  C1725UNLOCK;

  return OK;
}

int32_t
c1725StopRun(int32_t id)
{

  int32_t acq;

  printf("\nc1725: Stopping a run \n");

  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1725LOCK;
  acq = vmeRead32(&c1725p[id]->acq_ctrl) & ~(0x4);
  /*   acq = vmeRead32(&c1725p[id]->acq_ctrl) & (0xb); --- used to be this (Bryan)*/
  vmeWrite32(&c1725p[id]->acq_ctrl, acq);
  C1725UNLOCK;

  return OK;
}


int32_t
c1725Test1()
{

  int32_t myid=0;

  c1725Test1a(myid);

  c1725Test1b(myid);

  return 0;
}

int32_t
c1725Test1a(int32_t myid)
{

  if (c1725DefaultSetup(myid)==ERROR)
    {
      printf("C1725: ERROR: Cannot setup board.  Give up !\n");
      return ERROR;
    }

  taskDelay(1*60);

  c1725PrintStatus(myid);

  return OK;
}

int32_t
c1725Test1b(int32_t myid)
{

  /* After 1a, and prior to this, plug in the S-IN (False->True) */

  int32_t loop, maxloop, nev;

  maxloop = 500000;

  c1725SoftTrigger(myid);

  taskDelay(1*60);  /* wait N sec */

  loop=0;
  while (loop++ < maxloop)
    {
      if (c1725EventReady(myid)) break;
    }

  nev = c1725GetNumEv(myid);

  if (loop < maxloop)
    {
      printf("\nEvent ready \n");
    }
  else
    {
      printf("\nEvent NOT ready !\n");
    }

  printf("\n ----------------------------------------- \n Num of events  = %d     Size = %d  loop = %d \n",nev,c1725GetEventSize(myid),loop);

  c1725PrintStatus(myid);

  if (nev > 0 ) c1725PrintBuffer(myid);

  return 0;

}


int32_t
c1725Test2()
{

  int32_t myid=0;

  c1725Test2a(myid);

  /* may plug in trigger now */

  c1725Test2b(myid);

  if (c1725EventReady(myid)==1)
    {
      printf("\n -- Event is ready -- \n");
    }
  else
    {
      printf("\n -- Event NOT ready -- \n");
    }

  return 0;
}

int32_t
c1725Test2a(int32_t myid)
{

  // Preliminaries of Test 2

  // Use register-controlled mode.

  int32_t loop, maxloop, chan;
  int32_t my_acq_ctrl = 0;

  maxloop = 50000;

  c1725Reset(myid);

  loop=0;
  while (loop++ < maxloop)
    {
      if (c1725BoardReady(myid)) break;
    }

  c1725Clear(myid);

  for (chan=0; chan<8; chan++)
    {
      c1725SetChannelDAC(myid, chan, def_dac_val);
    }

  c1725SetBufOrg(myid, 4);
  c1725SetPostTrig(myid, 44);

  c1725SetAcqCtrl(myid, my_acq_ctrl);

  C1725LOCK;
  vmeWrite32(&c1725p[myid]->global_trigger_mask, 0xc0000000);
  vmeWrite32(&c1725p[myid]->config, 0x10);
  vmeWrite32(&c1725p[myid]->channel_enable_mask, 0xff);
  C1725UNLOCK;

  taskDelay(2*60);

  printf("\n ----- STATUS BEFORE RUN (2a)--------- \n");
  c1725PrintStatus(myid);

  return 0;
}


int32_t
c1725Test2b(int32_t myid)
{

  /* last part of Test2.  Do after plugging in trigger */

  int32_t loop;

  c1725StartRun(myid);

  loop=0;
  while (loop++ < 500000)
    {
      if (c1725EventReady(myid))
	{  /* should not be ready until 'StopRun' */
	  printf("Event Ready\n");
	  break;
	}
    }
  printf("Chk Event ready loop1 = %d \n",loop);

  printf("\n ----- STATUS AFTER RUN (2b) --------- \n");
  c1725PrintStatus(myid);

  printf("Num of events  = %d     Size = %d \n",c1725GetNumEv(myid),c1725GetEventSize(myid));

  if (c1725GetNumEv(myid)>0) c1725PrintBuffer(myid);


  return 0;

}

int32_t
c1725Test3()
{

  // Just checking registers

  int32_t myid=0;
  int32_t loop, maxloop;
  int32_t my_acq_ctrl = 0x2;

  maxloop = 50000;

  c1725Reset(myid);

  loop=0;
  while (loop++ < maxloop)
    {
      if (c1725BoardReady(myid)) break;
    }

  c1725Clear(myid);

  c1725SetBufOrg(myid, 2);

  c1725SetAcqCtrl(myid, my_acq_ctrl);

  taskDelay(4*60);

  printf("\n ----- STATUS --------- \n");
  c1725PrintStatus(myid);

  return 0;
}

int32_t
c1725TestPrintBuffer()
{
  /* test code (temp) */

  int32_t i;
  unsigned long laddr;
  uint32_t data;
  volatile uint32_t *bdata;
#ifdef VXWORKS
  sysBusToLocalAdrs(0x39,(char *)0x09000000,(char **)&laddr);
#else
  vmeBusToLocalAdrs(0x39,(char *)0x09000000,(char **)&laddr);
#endif
  bdata = (volatile uint32_t *)laddr;

  printf("\nTest Print\n");
  C1725LOCK;
  for (i=0; i<10; i++)
    {
      data = *bdata;
#ifndef VXWORKS
      data = LSWAP(data);
#endif
      printf("data[%d] = %d = 0x%x \n",i,data,data);
    }
  C1725UNLOCK;

  return OK;

}

int32_t
c1725PrintBuffer(int32_t id)
{

  int32_t ibuf,i;
  int32_t d1;

  if (c1725Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1725LOCK;
  for (ibuf=0; ibuf<5; ibuf++)
    {

      printf("c1725: Print Buf %d \n",ibuf);

      for (i=0; i<10; i++)
	{

	  d1 = vmeRead32(&c1725p[id]->readout_buffer[ibuf]);
	  printf("    Data[%d] = %d = 0x%x\n",i,d1,d1);

	}
    }
  C1725UNLOCK;

  return 0;
}


int32_t
c1725Test4(int32_t nloop)
{

  int32_t i,j;

  for (i=0; i<nloop; i++)
    {

      printf("\n\ndoing loop %d \n",i);
      c1725Test1();
      for (j=0; j<5000; j++)
	{
	  c1725BoardReady(0);
	}
      taskDelay(2*60);

    }
  return 0;

}
