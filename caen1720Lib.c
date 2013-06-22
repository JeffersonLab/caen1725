/*----------------------------------------------------------------------------*
 *  Copyright (c) 2013        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    Authors: Bryan Moffit                                                   *
 *             moffit@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5660             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *             Robert Michaels                                                *
 *             rom@jlab.org                      Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-7410             12000 Jefferson Ave.         *
 *                                               Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*
 *
 * Description:
 *     Library for the CAEN 1720 FADC 
 *
 * SVN: $Rev$
 *
 *----------------------------------------------------------------------------*/

#ifdef VXWORKS
#include <vxWorks.h>
#include <logLib.h>
#include <taskLib.h>
#include <intLib.h>
#include <iv.h>
#include <semLib.h>
#include <vxLib.h>
#include "vxCompat.h"
#else
#include "jvme.h"
#endif
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "caen1720Lib.h"

/* Mutex to guard TI read/writes */
pthread_mutex_t     c1720Mutex = PTHREAD_MUTEX_INITIALIZER;
#define C1720LOCK     if(pthread_mutex_lock(&c1720Mutex)<0) perror("pthread_mutex_lock");
#define C1720UNLOCK   if(pthread_mutex_unlock(&c1720Mutex)<0) perror("pthread_mutex_unlock");

/* Define external Functions */
#ifdef VXWORKS
IMPORT  STATUS sysBusToLocalAdrs (int, char *, char **);
#endif

/* Global variables */
int Nc1720 = 0;      /* Number of FADCs in crate */
volatile struct c1720_address *c1720p[C1720_MAX_BOARDS];  /* pointers to memory map */
static unsigned int c1720AddrOffset=0; /* offset between VME and local address */
static int c1720IntLevel=5;        /* default interrupt level */
static int c1720IntVector=0xa8;    /* default interrupt vector */

/* Some globals for test routines */
static int def_acq_ctrl=0x1;       /* default acq_ctrl */
static int def_dac_val=0x1000;     /* default DAC setting for each channel */


/*******************************************************************************
*
* c1720Init - Initialize CAEN 1720 Library. 
*
*   ARGS: 
*       addr:  VME address of the first module.  This can be:
*              <= 21        : Indicating the VME slot to use for CR-CSR addressing
*                             (*** Not supported in vxWorks ***)
*              < 0xFFFFFF   : Indicating the VME A24 address
*              < 0xFFFFFFFF : Indicating the VME A32 address 
*
*   addr_inc:  Incrementing address to initialize > 1 c1720
*
*       nadc:  Number of times to increment using addr_inc
*
*   RETURNS: OK, or ERROR if one or address increments resulted in ERROR
*.
*/

STATUS 
c1720Init(UINT32 addr, UINT32 addr_inc, int nadc) 
{

  int i, res, errFlag=0;
  int AMcode=0x39;
  int boardInfo=0, boardID;
  unsigned int laddr;

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
      res = sysBusToLocalAdrs (AMcode, (char *) addr, (char **) &laddr);
#else
      res = vmeBusToLocalAdrs (AMcode, (char *) addr, (char **) &laddr);
#endif

  c1720AddrOffset = laddr-addr;

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

  Nc1720 = 0;
  for (i = 0; i < nadc; i++) 
    {
      c1720p[i] = (struct c1720_address *) (laddr + i * addr_inc);
      /* Check if Board exists at that address */
#ifdef VXWORKS
      res = vxMemProbe ((char *) &(c1720p[i]->board_info), VX_READ, 4, (char *) &boardInfo);
#else
      res = vmeMemProbe ((char *) &(c1720p[i]->board_info), 4, (char *) &boardInfo);
#endif

      if (res < 0) 
	{
	  printf ("%s: ERROR: No addressable board at address = 0x%x\n",
		  __FUNCTION__,
		  (UINT32) c1720p[i] - c1720AddrOffset);
	  c1720p[i] = NULL;
	  errFlag = 1;
	  continue;
	}

      /* Check that this is a c1792 */
      boardID = ((vmeRead32(&c1720p[i]->rom.board0)<<16) |
		 (vmeRead32(&c1720p[i]->rom.board1)<<8) |
		 vmeRead32(&c1720p[i]->rom.board2));
      if((boardID & C1720_BOARD_ID_MASK)!=C1720_BOARD_ID)
	{
	  printf("%s: Invalid board type (0x%x) at address 0x%x\n",
		 __FUNCTION__,boardID, (UINT32) c1720p[i] - c1720AddrOffset);
	  c1720p[i] = NULL;
	  errFlag = 1;
	  continue;
	}
	
      
      Nc1720++;
      printf ("%s: Initialized ADC ID %d at address 0x%08x \n", __FUNCTION__,
	      i, (UINT32) c1720p[i] - c1720AddrOffset);
    }

  if (errFlag > 0)  
    {
      printf ("%s: ERROR: Unable to initialize all ADC Modules\n",__FUNCTION__);
      if (Nc1720 > 0) 
	printf ("%s: %d ADC (s) successfully initialized\n", __FUNCTION__, Nc1720);

      return ERROR;
    } 
  else 
    {
      return OK;
    }

  return OK;
}


inline int 
c1720Check(int id, const char *func) 
{

  if (!Nc1720 || id >= Nc1720) 
    {
      printf("%s: ERROR: Board %d not initialized \n",func,id);
      return ERROR;
    } 

  if(c1720p[id]==NULL) 
    {
      printf("%s: ERROR: Invalid pointer for board %d \n",func,id);
      return ERROR;
    }

  return OK;
}

/**************************************************************************************
 *
 * c1720PrintChanStatus  - Print channel registers to standard out
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int
c1720PrintChanStatus(int id, int chan) 
{
  unsigned int status=0, buffer_occupancy=0, fpga_firmware=0, dac=0, thresh=0;
  unsigned int time_overunder=0;

  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;
  if (chan < 0 || chan > 8) return ERROR;

  C1720LOCK;
  status           = vmeRead32(&c1720p[id]->chan[chan].status);
  buffer_occupancy = vmeRead32(&c1720p[id]->chan[chan].buffer_occupancy);
  fpga_firmware    = vmeRead32(&c1720p[id]->chan[chan].fpga_firmware);
  dac              = vmeRead32(&c1720p[id]->chan[chan].dac);
  thresh           = vmeRead32(&c1720p[id]->chan[chan].thresh);
  time_overunder   = vmeRead32(&c1720p[id]->chan[chan].time_overunder);
  C1720UNLOCK;

  printf("Channel %d   status (0x1%d88) = 0x%x \n",chan,chan,status);
  printf("      firmware (0x1%d8c) = 0x%x    buff. occ. (0x1%d94) = %d \n",
	 chan, fpga_firmware,chan, buffer_occupancy);
  printf("     dac (0x1%d98) = 0x%x    threshold (0x1%d84) = 0x%x \n",
	 chan, dac,chan, thresh);
  printf("     time_overunder = 0x%x\n",time_overunder);

  return OK;
}


/**************************************************************************************
 *
 * c1720PrintStatus  - Print module status to standard out
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int
c1720PrintStatus(int id) 
{
  unsigned int firmware, board_info, chan_config, buffer_org, buffer_size;
  unsigned int acq_ctrl, acq_status, reloc_addr, vme_status;
  unsigned int board_id, interrupt_id;
  unsigned int trigmask_enable, post_trigset;
  unsigned int c1720Base;
  int winwidth=0, winpost=0;
  int chan_print = 1;
  int ichan;

  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1720LOCK;
  firmware     = vmeRead32(&c1720p[id]->firmware);
  board_info   = vmeRead32(&c1720p[id]->board_info); 
  chan_config  = vmeRead32(&c1720p[id]->chan_config);
  buffer_org   = vmeRead32(&c1720p[id]->buffer_org);
  buffer_size  = vmeRead32(&c1720p[id]->buffer_size);
  acq_ctrl     = vmeRead32(&c1720p[id]->acq_ctrl);
  acq_status   = vmeRead32(&c1720p[id]->acq_status);
  reloc_addr   = vmeRead32(&c1720p[id]->reloc_addr);
  vme_status   = vmeRead32(&c1720p[id]->vme_status);
  board_id     = vmeRead32(&c1720p[id]->board_id);
  interrupt_id = vmeRead32(&c1720p[id]->interrupt_id);
  trigmask_enable = vmeRead32(&c1720p[id]->trigmask_enable);
  post_trigset = vmeRead32(&c1720p[id]->post_trigset);
  C1720UNLOCK;

  c1720Base = (unsigned int)c1720p[id];

  printf("\nStatus for CAEN 1720 board %d \n",id);
  printf("--------------------------------------------------------------------------------\n");
  printf("Firmware           (0x%04x) = 0x%08x\n",
	 (unsigned int)(&c1720p[id]->firmware)-c1720Base,firmware); 
  printf("Board info         (0x%04x) = 0x%08x\n",
	 (unsigned int)(&c1720p[id]->board_info)-c1720Base,board_info); 
  printf("Chan config        (0x%04x) = 0x%08x\n",
	 (unsigned int)(&c1720p[id]->chan_config)-c1720Base,chan_config);
  printf("Buffer org         (0x%04x) = 0x%08x\n",
	 (unsigned int)(&c1720p[id]->buffer_org)-c1720Base,buffer_org);
  printf("Buffer size (cust) (0x%04x) = 0x%08x\n",
	 (unsigned int)(&c1720p[id]->buffer_size)-c1720Base,buffer_size);
  printf("Post trig          (0x%04x) = 0x%08x\n",
	 (unsigned int)(&c1720p[id]->post_trigset)-c1720Base,post_trigset);
  printf("Acq control        (0x%04x) = 0x%08x\n",
	 (unsigned int)(&c1720p[id]->acq_ctrl)-c1720Base,acq_ctrl);
  printf("Acq status         (0x%04x) = 0x%08x\n",
	 (unsigned int)(&c1720p[id]->acq_status)-c1720Base,acq_status);
  printf("Relocation address (0x%04x) = 0x%08x\n",
	 (unsigned int)(&c1720p[id]->reloc_addr)-c1720Base,reloc_addr);
  printf("VME Status         (0x%04x) = 0x%08x\n",
	 (unsigned int)(&c1720p[id]->vme_status)-c1720Base,vme_status);
  printf("Board id           (0x%04x) = 0x%08x\n",
	 (unsigned int)(&c1720p[id]->board_id)-c1720Base,board_id);
  printf("Interrupt id       (0x%04x) = 0x%08x\n",
	 (unsigned int)(&c1720p[id]->interrupt_id)-c1720Base,interrupt_id);
  printf("TrigSrc Mask       (0x%04x) = 0x%08x\n",
	 (unsigned int)(&c1720p[id]->trigmask_enable)-c1720Base,trigmask_enable);
  printf("\n");

  printf("ROC FPGA Firmware version: %d.%d\n",(firmware&0xFF00)>>8, firmware&0xFF);
  printf("Channel Configuration: \n");
  printf(" - Trigger Overlapping: %s\n",
	 (chan_config&C1720_CHAN_CONFIG_TRIG_OVERLAP) ? "on" : "off");
  printf(" - Trigger for %s threshold\n",
	 (chan_config&C1720_CHAN_CONFIG_TRIGOUT_UNDER_THRESHOLD) ? "UNDER" : "OVER");
  printf(" - Pack2.5 Encoding: %s\n",
	 (chan_config&C1720_CHAN_CONFIG_PACK2_5) ? "on" : "off");
  if(chan_config&C1720_CHAN_CONFIG_ZLE)
    printf(" - Zero Length Encoding: on\n");
  if(chan_config&C1720_CHAN_CONFIG_ZS_AMP)
    printf(" - Amplitude based full suppression encoding: on\n");

  printf("\n\n");
  if (chan_print) 
    {
      for (ichan = 0; ichan < 8; ichan++) 
	{
	  c1720PrintChanStatus(id,ichan);
	}
    }

  printf("--------------------------------------------------------------------------------\n");

  return OK;
 
}

/**************************************************************************************
 *
 * c1720Reset  - reset the board -- clear output buffer, event counter,
 *      and performs a FPGAs global reset to restore FPGAs to 
 *      their default config.  Also initializes counters to 
 *      their initial state and clears all error conditions.
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int
c1720Reset(int id) 
{
  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1720LOCK;
  vmeWrite32(&c1720p[id]->sw_reset, 1);
  vmeWrite32(&c1720p[id]->vme_ctrl, 0x10);
  vmeWrite32(&c1720p[id]->enable_mask, 0xff);
  C1720UNLOCK;

  return OK;

}

/**************************************************************************************
 *
 * c1720Clear  - Clear the output buffer
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int
c1720Clear(int id) 
{
  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1720LOCK;
  vmeWrite32(&c1720p[id]->sw_clear, 1);
  C1720UNLOCK;
  c1720SetAcqCtrl(id, 0);

  return OK;

}

/**************************************************************************************
 *
 * c1720SoftTrigger  - Generate a software trigger.  Software trigger must be
 *     enabled (with c1720EnableTriggerSource(...))
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int 
c1720SoftTrigger(int id) 
{

  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1720LOCK;
  vmeWrite32(&c1720p[id]->sw_trigger, 1);
  C1720UNLOCK;

  return OK;

}

/**************************************************************************************
 *
 * c1720SetTriggerOverlapping  - Enable/Disable trigger overlapping feature
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int
c1720SetTriggerOverlapping(int id, int enable)
{
  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1720LOCK;
  if(enable)
    vmeWrite32(&c1720p[id]->config_bitset, C1720_CHAN_CONFIG_TRIG_OVERLAP);
  else
    vmeWrite32(&c1720p[id]->config_bitclear, C1720_CHAN_CONFIG_TRIG_OVERLAP);
  C1720UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1720SetTestPatternGeneration  - Enable/Disable Test Pattern Generation
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int
c1720SetTestPatternGeneration(int id, int enable)
{
  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1720LOCK;
  if(enable)
    vmeWrite32(&c1720p[id]->config_bitset, C1720_CHAN_CONFIG_TEST_PATTERN);
  else
    vmeWrite32(&c1720p[id]->config_bitclear, C1720_CHAN_CONFIG_TEST_PATTERN);
  C1720UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1720SetTriggerOnUnderThreshold  - Enable/Disable triggering on "under" threshold
 *         (as opposed to "over" threshold)
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int
c1720SetTriggerOnUnderThreshold(int id, int enable)
{
  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1720LOCK;
  if(enable)
    vmeWrite32(&c1720p[id]->config_bitset, C1720_CHAN_CONFIG_TRIGOUT_UNDER_THRESHOLD);
  else
    vmeWrite32(&c1720p[id]->config_bitclear, C1720_CHAN_CONFIG_TRIGOUT_UNDER_THRESHOLD);
  C1720UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1720SetPack2_5  - Enable/Disable Pack2.5 data encoding (2.5 samples per 32bit
 *       data word)
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int
c1720SetPack2_5(int id, int enable)
{
  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1720LOCK;
  if(enable)
    vmeWrite32(&c1720p[id]->config_bitset, C1720_CHAN_CONFIG_PACK2_5);
  else
    vmeWrite32(&c1720p[id]->config_bitclear, C1720_CHAN_CONFIG_PACK2_5);
  C1720UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1720SetZeroLengthEncoding  - Enable/Disable Zero Length Encoding (ZLE).
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int
c1720SetZeroLengthEncoding(int id, int enable)
{
  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1720LOCK;
  if(enable)
    vmeWrite32(&c1720p[id]->config_bitset, C1720_CHAN_CONFIG_ZLE);
  else
    vmeWrite32(&c1720p[id]->config_bitclear, C1720_CHAN_CONFIG_ZLE);
  C1720UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1720SetAmplitudeBasedFullSuppression  - Enable/Disable Full Suppression based
 *     on the amplitude of the signal.
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int
c1720SetAmplitudeBasedFullSuppression(int id, int enable)
{
  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1720LOCK;
  if(enable)
    vmeWrite32(&c1720p[id]->config_bitset, C1720_CHAN_CONFIG_ZS_AMP);
  else
    vmeWrite32(&c1720p[id]->config_bitclear, C1720_CHAN_CONFIG_ZS_AMP);
  C1720UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1720EnableTriggerSource  - Enable a trigger source
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

int
c1720EnableTriggerSource(int id, int src, int chanmask, int level)
{
  int enablebits=0, prevbits=0;
  int setlevel=0;
  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  switch(src)
    {
    case C1720_SOFTWARE_TRIGGER_ENABLE:
      {
	enablebits = C1720_TRIGMASK_ENABLE_SOFTWARE;
	printf("%s: Enabling Software triggers\n",__FUNCTION__);
	break;
      }

    case C1720_EXTERNAL_TRIGGER_ENABLE:
      {
	enablebits = C1720_TRIGMASK_ENABLE_EXTERNAL;
	printf("%s: Enabling External triggers\n",__FUNCTION__);
	break;
      }

    case C1720_CHANNEL_TRIGGER_ENABLE:
      {
	if(chanmask > C1720_TRIGMASK_ENABLE_CHANNEL_MASK)
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

    case C1720_ALL_TRIGGER_ENABLE:
    default:
      {
	if(chanmask > C1720_TRIGMASK_ENABLE_CHANNEL_MASK)
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

	enablebits  = C1720_TRIGMASK_ENABLE_SOFTWARE;
	enablebits |= C1720_TRIGMASK_ENABLE_EXTERNAL;
	enablebits |= chanmask;
	enablebits |= (level<<24);
	setlevel=1;
	printf("%s: Enabling Software, External, and Channel triggers\n",__FUNCTION__);
	printf("\t(mask=0x%02x, coincidence level = %d)\n",chanmask,level);
      }

    } /* switch(src) */


  C1720LOCK;
  prevbits = vmeRead32(&c1720p[id]->trigmask_enable);

  if(setlevel)
    { /* enablebits contains a new coincidence level */
      enablebits = (prevbits & ~C1720_TRIGMASK_ENABLE_COINC_LEVEL_MASK) | enablebits;
    }
  else
    { /* leave coincidence level unchanged */
      enablebits = (prevbits | enablebits);
    }

  vmeWrite32(&c1720p[id]->trigmask_enable, enablebits);
  C1720UNLOCK;
  
  return OK;
}

/**************************************************************************************
 *
 * c1720DisableTriggerSource  - Disable a trigger source
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

int 
c1720DisableTriggerSource(int id, int src, int chanmask)
{
  unsigned int disablebits=0;
  if (c1720Check(id,__FUNCTION__)==ERROR) return 0;

  switch(src)
    {
    case C1720_SOFTWARE_TRIGGER_ENABLE:
      {
	disablebits = C1720_TRIGMASK_ENABLE_SOFTWARE;
	printf("%s: Disabling Software triggers\n",__FUNCTION__);
	break;
      }

    case C1720_EXTERNAL_TRIGGER_ENABLE:
      {
	disablebits = C1720_TRIGMASK_ENABLE_EXTERNAL;
	printf("%s: Disabling External triggers\n",__FUNCTION__);
	break;
      }

    case C1720_CHANNEL_TRIGGER_ENABLE:
      {
	if(chanmask > C1720_TRIGMASK_ENABLE_CHANNEL_MASK)
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

    case C1720_ALL_TRIGGER_ENABLE:
    default:
      {
	if(chanmask > C1720_TRIGMASK_ENABLE_CHANNEL_MASK)
	  {
	    printf("%s: ERROR: Invalid channel mask (0x%x)\n",
		   __FUNCTION__,chanmask);
	    return ERROR;
	  }

	disablebits  = C1720_TRIGMASK_ENABLE_SOFTWARE;
	disablebits |= C1720_TRIGMASK_ENABLE_EXTERNAL;
	disablebits |= chanmask;

	printf("%s: Disabling Software, External, and Channel triggers\n",__FUNCTION__);
	printf("\t(mask=0x%02x)\n",chanmask);
      }

    } /* switch(src) */

  C1720LOCK;
  vmeWrite32(&c1720p[id]->trigmask_enable, 
	     vmeRead32(&c1720p[id]->trigmask_enable) & ~disablebits);
  C1720UNLOCK;


  return OK;
}

/**************************************************************************************
 *
 * c1720EnableFPTrigOut  - Enable the trigger output on the front panel from 
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

int
c1720EnableFPTrigOut(int id, int src, int chanmask)
{
  int enablebits=0;
  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  switch(src)
    {
    case C1720_SOFTWARE_TRIGGER_ENABLE:
      {
	enablebits = C1720_TRIGMASK_ENABLE_SOFTWARE;
	break;
      }

    case C1720_EXTERNAL_TRIGGER_ENABLE:
      {
	enablebits = C1720_TRIGMASK_ENABLE_EXTERNAL;
	break;
      }

    case C1720_CHANNEL_TRIGGER_ENABLE:
      {
	if(chanmask > C1720_TRIGMASK_ENABLE_CHANNEL_MASK)
	  {
	    printf("%s: ERROR: Invalid channel mask (0x%x)\n",
		   __FUNCTION__,chanmask);
	    return ERROR;
	  }

	enablebits = chanmask;
	break;
      }

    case C1720_ALL_TRIGGER_ENABLE:
    default:
      {
	if(chanmask > C1720_TRIGMASK_ENABLE_CHANNEL_MASK)
	  {
	    printf("%s: ERROR: Invalid channel mask (0x%x)\n",
		   __FUNCTION__,chanmask);
	    return ERROR;
	  }

	enablebits  = C1720_TRIGMASK_ENABLE_SOFTWARE;
	enablebits |= C1720_TRIGMASK_ENABLE_EXTERNAL;
	enablebits |= chanmask;
      }

    } /* switch(src) */


  C1720LOCK;
  vmeWrite32(&c1720p[id]->tmask_out,
	     vmeRead32(&c1720p[id]->tmask_out) | enablebits);
  C1720UNLOCK;
  
  return OK;
}

/**************************************************************************************
 *
 *  c1720DisableFPTrigOut - Disable a trigger signal contributing to the trigger
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

int 
c1720DisableFPTrigOut(int id, int src, int chanmask)
{
  unsigned int disablebits=0;
  if (c1720Check(id,__FUNCTION__)==ERROR) return 0;

  switch(src)
    {
    case C1720_SOFTWARE_TRIGGER_ENABLE:
      {
	disablebits = C1720_TRIGMASK_ENABLE_SOFTWARE;
	break;
      }

    case C1720_EXTERNAL_TRIGGER_ENABLE:
      {
	disablebits = C1720_TRIGMASK_ENABLE_EXTERNAL;
	break;
      }

    case C1720_CHANNEL_TRIGGER_ENABLE:
      {
	if(chanmask > C1720_TRIGMASK_ENABLE_CHANNEL_MASK)
	  {
	    printf("%s: ERROR: Invalid channel mask (0x%x)\n",
		   __FUNCTION__,chanmask);
	    return ERROR;
	  }

	disablebits = chanmask;
	break;
      }

    case C1720_ALL_TRIGGER_ENABLE:
    default:
      {
	if(chanmask > C1720_TRIGMASK_ENABLE_CHANNEL_MASK)
	  {
	    printf("%s: ERROR: Invalid channel mask (0x%x)\n",
		   __FUNCTION__,chanmask);
	    return ERROR;
	  }

	disablebits  = C1720_TRIGMASK_ENABLE_SOFTWARE;
	disablebits |= C1720_TRIGMASK_ENABLE_EXTERNAL;
	disablebits |= chanmask;
      }

    } /* switch(src) */

  C1720LOCK;
  vmeWrite32(&c1720p[id]->tmask_out, 
	     vmeRead32(&c1720p[id]->tmask_out) & ~disablebits);
  C1720UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1720SetEnableChannelMask  - Set which channels provide the samples which are
 *     stored into the events.
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int
c1720SetEnableChannelMask(int id, int chanmask)
{
  if (c1720Check(id,__FUNCTION__)==ERROR) return 0;

  if(chanmask>C1720_ENABLE_CHANNEL_MASK)
    {
      printf("%s: ERROR: Invalid channel mask (0x%x)\n",
	     __FUNCTION__,chanmask);
      return ERROR;
    }

  C1720LOCK;
  vmeWrite32(&c1720p[id]->enable_mask,chanmask);
  C1720UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1720GetEventSize  - Obtain the number of 32bit words in the next event.
 *
 * RETURNS: Event Size if successful, ERROR otherwise.
 *
 */

unsigned int 
c1720GetEventSize(int id) 
{
  unsigned int rval=0;

  if (c1720Check(id,__FUNCTION__)==ERROR) return 0;

  C1720LOCK;
  rval = vmeRead32(&c1720p[id]->event_size);
  C1720UNLOCK;

  return rval; 
}


/**************************************************************************************
 *
 * c1720GetNumEv  - Obtain the number of events current stored in the output buffer
 *
 * RETURNS: Number of events if successful, ERROR otherwise.
 *
 */

unsigned int 
c1720GetNumEv(int id) 
{
  unsigned int rval=0;

  if (c1720Check(id,__FUNCTION__)==ERROR) return 0;

  C1720LOCK;
  rval = vmeRead32(&c1720p[id]->event_stored);
  C1720UNLOCK;

  return rval; 

}

/**************************************************************************************
 *
 * c1720SetChannelDAC  - Set the DC offset to be added to the input signal.
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int 
c1720SetChannelDAC(int id, int chan, int dac) 
{
  int iwait=0, maxwait=1000;
  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;
  if (chan < 0 || chan > 8) return ERROR;

  printf("%s: Writing DAC for id=%d  chan=%d   value=%d\n",
	 __FUNCTION__,id,chan,dac);

  C1720LOCK;
  vmeWrite32(&c1720p[id]->chan[chan].dac, dac);
  while(iwait<maxwait)
    {
      if((vmeRead32(&c1720p[id]->chan[chan].status) & C1720_CHANNEL_STATUS_BUSY)==0)
	break;
      iwait++;
    }
  C1720UNLOCK;
  if(iwait>=maxwait)
    {
      printf("%s: ERROR: Timeout in setting the DAC\n",__FUNCTION__);
      return ERROR;
    }

  return OK;

}

/**************************************************************************************
 *
 * c1720BufferFree  - Frees the first specified number of output buffer memory blocks.
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int 
c1720BufferFree(int id, int num) 
{

  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  printf("%s: INFO: Freeing = %d output buffer memory blocks \n",__FUNCTION__,num);

  C1720LOCK;
  vmeWrite32(&c1720p[id]->buffer_free, num);
  C1720UNLOCK;

  return OK;

}

/**************************************************************************************
 *
 * c1720SetAcqCtrl  - Set the acquisition control register
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int 
c1720SetAcqCtrl(int id, int bits) 
{

  unsigned int acq;

  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1720LOCK;
  acq = vmeRead32(&c1720p[id]->acq_ctrl);
  vmeWrite32(&c1720p[id]->acq_ctrl, (acq | bits));
  C1720UNLOCK;
 
  return OK;
}

/**************************************************************************************
 *
 * c1720BoardReady  - Determine if the module is ready for acquisition.
 *
 * RETURNS: 1 if ready, 0 if not ready, ERROR otherwise.
 *
 */

int 
c1720BoardReady(int id) 
{
  unsigned int rval=0;

  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1720LOCK;
  rval = (vmeRead32(&c1720p[id]->acq_status) & C1720_ACQ_STATUS_ACQ_READY)>>8;
  C1720UNLOCK;

  return rval;
}

/**************************************************************************************
 *
 * c1720EventReady  - Determine if at least one event is ready for readout
 *
 * RETURNS: 1 if data is ready, 0 if not, ERROR otherwise.
 *
 */

int 
c1720EventReady(int id) 
{

  unsigned int status1=0, status2=0;
  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;
  
  C1720LOCK;
  status1 = (vmeRead32(&c1720p[id]->acq_status) & C1720_ACQ_STATUS_EVENT_READY);
  status2 = (vmeRead32(&c1720p[id]->vme_status) & C1720_VME_STATUS_EVENT_READY);
  C1720UNLOCK;

  if (status1 && status2) 
    return 1;
  
  return 0;
}

/**************************************************************************************
 *
 * c1720SetBufOrg  - Set the organization of blocks in the output buffer memory
 *     See Manual for code to memory division translation table.
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int 
c1720SetBufOrg(int id, int code) 
{

  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1720LOCK;
  vmeWrite32(&c1720p[id]->buffer_org, code);
  C1720UNLOCK;
 
  return OK;
}

/**************************************************************************************
 *
 * c1720SetBufferSize  - Set the custom buffer size
 *       This equates to the total number of 32bit data words per channel.
 *       How this translates to number of samples, depends on the encoding.
 *       For normal encoding, val=1 -> 2 samples
 *           Pack2.5          val=1 -> 2.5 samples
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int 
c1720SetBufferSize(int id, int val) 
{

  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1720LOCK;
  vmeWrite32(&c1720p[id]->buffer_size, val);
  C1720UNLOCK;
 
  return OK;
}

/**************************************************************************************
 *
 * c1720SetPostTrig  - Set the Post Trigger Setting register
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int 
c1720SetPostTrig(int id, int val) 
{

  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1720LOCK;
  vmeWrite32(&c1720p[id]->post_trigset, val);
  C1720UNLOCK;

  return OK;

}

/**************************************************************************************
 *
 * c1720SetBusError  - Enable/Disable Bus Error termination for block transfers.
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int
c1720SetBusError(int id, int enable)
{
  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1720LOCK;
  if(enable)
    {
      vmeWrite32(&c1720p[id]->vme_ctrl, 
		 vmeRead32(&c1720p[id]->vme_ctrl) | C1720_VME_CTRL_BERR_ENABLE);
    }
  else
    {
      vmeWrite32(&c1720p[id]->vme_ctrl, 
		 vmeRead32(&c1720p[id]->vme_ctrl) & ~C1720_VME_CTRL_BERR_ENABLE);
    }
  C1720UNLOCK;
 
  return OK;
}

/**************************************************************************************
 *
 * c1720SetAlign64  - Enable/disable 64bit alignment for data words in a block transfer
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int
c1720SetAlign64(int id, int enable)
{
  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1720LOCK;
  if(enable)
    {
      vmeWrite32(&c1720p[id]->vme_ctrl, 
		 vmeRead32(&c1720p[id]->vme_ctrl) | C1720_VME_CTRL_ALIGN64_ENABLE);
    }
  else
    {
      vmeWrite32(&c1720p[id]->vme_ctrl, 
		 vmeRead32(&c1720p[id]->vme_ctrl) & ~C1720_VME_CTRL_ALIGN64_ENABLE);
    }
  C1720UNLOCK;
 
  return OK;
}

/**************************************************************************************
 *
 * c1720SetChannelThreshold  - Set the channel threshold used for trigger and/or
 *     data suppression.
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int
c1720SetChannelThreshold(int id, int chan, int thresh)
{
  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  if((chan<0) || (chan>7))
    {
      printf("%s: ERROR: Invalid channel (%d)\n",
	     __FUNCTION__,chan);
      return ERROR;
    }
  
  if(thresh>C1720_CHANNEL_THRESHOLD_MASK)
    {
      printf("%s: ERROR: Invalid threshold (%d)\n",
	     __FUNCTION__,thresh);
      return ERROR;
    }
  
  C1720LOCK;
  vmeWrite32(&c1720p[id]->chan[chan].thresh, thresh);
  C1720UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1720SetChannelTimeOverUnder  - Set the channel samples over/under threshold to 
 *     generate a trigger.
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int
c1720SetChannelTimeOverUnder(int id, int chan, int samp)
{
  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  if((chan<0) || (chan>7))
    {
      printf("%s: ERROR: Invalid channel (%d)\n",
	     __FUNCTION__,chan);
      return ERROR;
    }
  
  if(samp>C1720_CHANNEL_TIME_OVERUNDER_MASK)
    {
      printf("%s: ERROR: Invalid threshold (%d)\n",
	     __FUNCTION__,samp);
      return ERROR;
    }
  
  C1720LOCK;
  vmeWrite32(&c1720p[id]->chan[chan].time_overunder, samp);
  C1720UNLOCK;

  return OK;
}




/**************************************************************************************
 *
 * c1720SetMonitorMode  - Set the mode of the front panel monitor output
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

int
c1720SetMonitorMode(int id, int mode)
{
  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  if((mode>4) || (mode==2))
    {
      printf("%s: ERROR: Invalid mode (%d)\n",
	     __FUNCTION__,mode);
      return ERROR;
    }

  C1720LOCK;
  vmeWrite32(&c1720p[id]->monitor_mode, mode);
  C1720UNLOCK;

  return OK;
}


/**************************************************************************************
 *
 * c1720SetMonitorDAC  - Set the DAC value for the front panel monitor output
 *           -- Relevant for Monitor Mode 4
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int
c1720SetMonitorDAC(int id, int dac)
{
  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  if(dac>C1720_MONITOR_DAC_MASK)
    {
      printf("%s: ERROR: Invalid dac (%d)\n",
	     __FUNCTION__,dac);
      return ERROR;
    }

  C1720LOCK;
  vmeWrite32(&c1720p[id]->monitor_dac, dac);
  C1720UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1720SetupInterrupt  - Set the interrupt level and vector.
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int
c1720SetupInterrupt(int id, int level, int vector)
{
  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  if(level==0)
    {
      printf("%s: ERROR: Invalid interrupt level (%d)\n",
	     __FUNCTION__,level);
      return ERROR;
    }

  C1720LOCK;
  vmeWrite32(&c1720p[id]->interrupt_id,vector);
  c1720IntVector = vector;
  c1720IntLevel = level;
  C1720UNLOCK;

  return OK;
}

/**************************************************************************************
 *
 * c1720EnableInterrupts  - Enable interrupt generation on trigger
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int
c1720EnableInterrupts(int id)
{
  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1720LOCK;
  vmeWrite32(&c1720p[id]->vme_ctrl, 
	     (vmeRead32(&c1720p[id]->vme_ctrl) &~C1720_VME_CTRL_INTLEVEL_MASK) 
	     | c1720IntLevel);
  C1720UNLOCK;
  
  return OK;
}

/**************************************************************************************
 *
 * c1720DisableInterrupts  - Disable interrupt generation
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */

int
c1720DisableInterrupts(int id)
{
  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  C1720LOCK;
  vmeWrite32(&c1720p[id]->vme_ctrl, 
	     (vmeRead32(&c1720p[id]->vme_ctrl) &~C1720_VME_CTRL_INTLEVEL_MASK));
  C1720UNLOCK;
  
  return OK;

}

/**************************************************************************************
 *
 *  c1720ReadEvent - General Data readout routine
 *
 *    id    - ID number of module to read
 *    data  - local memory address to place data
 *    nwrds - Max number of words to transfer
 *    rflag - Readout Flag
 *              0 - programmed I/O from the specified board
 *              1 - DMA transfer using Universe/Tempe DMA Engine 
 *                    (DMA VME transfer Mode must be setup prior)
 */

int
c1720ReadEvent(int id, volatile unsigned int *data, int nwrds, int rflag)
{
  int dCnt=0;
  unsigned int tmpData=0, evLen=0;
  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR;

  if(data==NULL) 
    {
      logMsg("c1720ReadEvent: ERROR: Invalid Destination address\n",0,0,0,0,0,0);
      return ERROR;
    }

  C1720LOCK;
  if(rflag==0)
    { /* Programmed I/O */
      /* First word should be the header */
      tmpData = vmeRead32(&c1720p[id]->readout_buffer[0]);
      if( (tmpData & C1720_HEADER_TYPE_MASK) != 0xA0000000)
	{
	  logMsg("c1720ReadEvent: ERROR: Invalid Header Word (0x%08x) for id = %d\n",
		 tmpData,id,3,4,5,6);
	  C1720UNLOCK;
	  return ERROR;
	}

      evLen = tmpData & C1720_HEADER_EVENTSIZE_MASK;
#ifdef VXWORKS
      data[dCnt++] = tmpData;
#else
      data[dCnt++] = LSWAP(tmpData);
#endif
      while(dCnt<evLen)
	{
	  /* Do not byteswap here (Linux), to make it consistent with DMA */
	  data[dCnt] = c1720p[id]->readout_buffer[0];;
	  dCnt++;
	  if(dCnt>=nwrds)
	    {
	      logMsg("c1720ReadEvent: WARN: Transfer limit reached.  nwrds = %d, evLen = %d, dCnt = %d\n",
		     nwrds, evLen, dCnt,4,5,6);
	      C1720UNLOCK;
	      return dCnt;
	    }
	}

      C1720UNLOCK;
      return dCnt;
    } /* rflag == 0 */
  else /* rflag == ? */
    {
      logMsg("c1720ReadEvent: ERROR: Unsupported readout flag (%d)\n",
	     rflag,2,3,4,5,6);
      C1720UNLOCK;
      return ERROR;
    }

  return OK;
}

/* Start of some test code */
  
int
c1720DefaultSetup(int id) 
{

  int loop, maxloop, chan;
  maxloop = 10000;

  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR; 

  c1720Reset(id);

  loop=0;
  while (loop++ < maxloop) 
    {
      if (c1720BoardReady(id)) break;
    }

  c1720Clear(id);
  c1720StopRun(id);
  
  c1720SetBufOrg(id, 4);  /* #buffers = 2^N */
  c1720SetPostTrig(id, 40);

  c1720SetAcqCtrl(id, def_acq_ctrl);

  /* Following two are defaults after reset anyway (i.e. not necessary
     to set them here) */
  /*  c1720p[id]->trigmask_enable = 0xc0000000; 
      c1720p[id]->enable_mask = 0xff; */

  C1720LOCK;
  vmeWrite32(&c1720p[id]->chan_config, 0x10);
  C1720UNLOCK;

  for (chan=0; chan<8; chan++) 
    {
      c1720SetChannelDAC(id, chan, def_dac_val);    
    }

  return OK;

} 

int 
c1720StartRun(int id) 
{

  int acq;

  printf("\nc1720: Starting a run \n");

  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR; 

  C1720LOCK;
  acq = vmeRead32(&c1720p[id]->acq_ctrl);
  vmeWrite32(&c1720p[id]->acq_ctrl, (acq | 0x4));
  C1720UNLOCK;

  return OK;
} 

int 
c1720StopRun(int id) 
{

  int acq;

  printf("\nc1720: Stopping a run \n");

  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR; 

  C1720LOCK;
  acq = vmeRead32(&c1720p[id]->acq_ctrl) & ~(0x4);
  /*   acq = vmeRead32(&c1720p[id]->acq_ctrl) & (0xb); --- used to be this (Bryan)*/
  vmeWrite32(&c1720p[id]->acq_ctrl, acq);
  C1720UNLOCK;

  return OK;
} 


int 
c1720Test1() 
{
  
  int myid=0;

  c1720Test1a(myid);

  c1720Test1b(myid);

  return 0;
}

int 
c1720Test1a(int myid) 
{

  if (c1720DefaultSetup(myid)==ERROR) 
    {
      printf("C1720: ERROR: Cannot setup board.  Give up !\n");
      return ERROR;
    }

  taskDelay(1*60);

  c1720PrintStatus(myid);

  return OK;
}

int 
c1720Test1b(int myid) 
{

  /* After 1a, and prior to this, plug in the S-IN (False->True) */

  int loop, maxloop, nev;

  maxloop = 500000;

  c1720SoftTrigger(myid); 

  taskDelay(1*60);  /* wait N sec */
  
  loop=0;
  while (loop++ < maxloop) 
    {
      if (c1720EventReady(myid)) break;
    }

  nev = c1720GetNumEv(myid);

  if (loop < maxloop) 
    {
      printf("\nEvent ready \n");
    } 
  else 
    {
      printf("\nEvent NOT ready !\n");
    }  

  printf("\n ----------------------------------------- \n Num of events  = %d     Size = %d  loop = %d \n",nev,c1720GetEventSize(myid),loop);

  c1720PrintStatus(myid);

  if (nev > 0 ) c1720PrintBuffer(myid);

  return 0;

}


int
c1720Test2() 
{

  int myid=0;

  c1720Test2a(myid);

  /* may plug in trigger now */

  c1720Test2b(myid);

  if (c1720EventReady(myid)==1) 
    {
      printf("\n -- Event is ready -- \n");
    } 
  else 
    {
      printf("\n -- Event NOT ready -- \n");
    }

  return 0;
}

int
c1720Test2a(int myid) 
{

  // Preliminaries of Test 2

  // Use register-controlled mode.
  
  int loop, maxloop, chan;
  int my_acq_ctrl = 0;

  maxloop = 50000;

  c1720Reset(myid);

  loop=0;
  while (loop++ < maxloop) 
    {
      if (c1720BoardReady(myid)) break;
    }

  c1720Clear(myid); 

  for (chan=0; chan<8; chan++) 
    {
      c1720SetChannelDAC(myid, chan, def_dac_val);    
    }
 
  c1720SetBufOrg(myid, 4);
  c1720SetPostTrig(myid, 44);

  c1720SetAcqCtrl(myid, my_acq_ctrl);

  C1720LOCK;
  vmeWrite32(&c1720p[myid]->trigmask_enable, 0xc0000000);
  vmeWrite32(&c1720p[myid]->chan_config, 0x10);
  vmeWrite32(&c1720p[myid]->enable_mask, 0xff);
  C1720UNLOCK;

  taskDelay(2*60);

  printf("\n ----- STATUS BEFORE RUN (2a)--------- \n");
  c1720PrintStatus(myid);

  return 0;
}


int
c1720Test2b(int myid) 
{

  /* last part of Test2.  Do after plugging in trigger */

  int loop;

  c1720StartRun(myid);

  loop=0;
  while (loop++ < 500000) 
    {
      if (c1720EventReady(myid)) 
	{  /* should not be ready until 'StopRun' */
	  printf("Event Ready\n");
	  break;
	}
    }
  printf("Chk Event ready loop1 = %d \n",loop);

  printf("\n ----- STATUS AFTER RUN (2b) --------- \n");
  c1720PrintStatus(myid);

  printf("Num of events  = %d     Size = %d \n",c1720GetNumEv(myid),c1720GetEventSize(myid));

  if (c1720GetNumEv(myid)>0) c1720PrintBuffer(myid);


  return 0;

}

int 
c1720Test3() 
{

  // Just checking registers
  
  int myid=0;
  int loop, maxloop;
  int my_acq_ctrl = 0x2;

  maxloop = 50000;

  c1720Reset(myid);

  loop=0;
  while (loop++ < maxloop) 
    {
      if (c1720BoardReady(myid)) break;
    }

  c1720Clear(myid); 
 
  c1720SetBufOrg(myid, 2);

  c1720SetAcqCtrl(myid, my_acq_ctrl);

  taskDelay(4*60);

  printf("\n ----- STATUS --------- \n");
  c1720PrintStatus(myid);

  return 0;
}

int
c1720TestPrintBuffer() 
{
  /* test code (temp) */

  int i;
  unsigned int laddr, data;
  volatile unsigned int *bdata;
#ifdef VXWORKS
  sysBusToLocalAdrs(0x39,(char *)0x09000000,(char **)&laddr);
#else
  vmeBusToLocalAdrs(0x39,(char *)0x09000000,(char **)&laddr);
#endif
  bdata = (volatile unsigned int *)laddr;

  printf("\nTest Print\n");
  C1720LOCK;
  for (i=0; i<10; i++) 
    {
      data = *bdata;
#ifndef VXWORKS
      data = LSWAP(data);
#endif
      printf("data[%d] = %d = 0x%x \n",i,data,data);
    } 
  C1720UNLOCK;

  return OK; 

}

int
c1720PrintBuffer(int id) 
{

  int ibuf,i;
  int d1;

  if (c1720Check(id,__FUNCTION__)==ERROR) return ERROR; 

  C1720LOCK;
  for (ibuf=0; ibuf<5; ibuf++) 
    {

      printf("c1720: Print Buf %d \n",ibuf);

      for (i=0; i<10; i++) 
	{

	  d1 = vmeRead32(&c1720p[id]->readout_buffer[ibuf]);
	  printf("    Data[%d] = %d = 0x%x\n",i,d1,d1);

	}
    }
  C1720UNLOCK;

  return 0;
}


int
c1720Test4(int nloop) 
{

  int i,j;

  for (i=0; i<nloop; i++) 
    {

      printf("\n\ndoing loop %d \n",i);
      c1720Test1();
      for (j=0; j<5000; j++) 
	{
	  c1720BoardReady(0);
	}
      taskDelay(2*60);

    }
  return 0;

}
