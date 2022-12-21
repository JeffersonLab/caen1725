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
static int32_t Nc1725 = 0;      /* Number of FADCs in crate */
volatile c1725_address *c1725p[MAX_VME_SLOTS+1];  /* pointers to memory map */
volatile c1725_address *c1725MCSTp=NULL;    /* pointer to MCST memory map */
static int32_t c1725ID[MAX_VME_SLOTS+1];                    /**< array of slot numbers */
static unsigned long c1725AddrOffset=0; /* offset between VME and local address */
static unsigned long c1725MCSTOffset=0; /* offset between VME and local address */
static uint32_t c1725MCSTBase = 0x09000000;
static int32_t c1725IntLevel=5;        /* default interrupt level */
static int32_t c1725IntVector=0xa8;    /* default interrupt vector */

/* Some globals for test routines */
static int32_t def_acq_ctrl=0x1;       /* default acq_ctrl */
static int32_t def_dac_val=0x1000;     /* default DAC setting for each channel */

#define CHECKID(_id)							\
  if((_id<0) || (_id>=MAX_VME_SLOTS) || (c1725p[_id] == NULL))		\
    {									\
      fprintf(stderr, "%s: ERROR: CAEN1725 id %d is not initialized \n", \
	      __func__, _id);						\
      return ERROR;							\
    }
#define CHECKCHAN(_chan)						\
  if((_chan<0) || (_chan>C1725_MAX_ADC_CHANNELS))			\
    {									\
      fprintf(stderr, "%s: ERROR: Invalid channel (%d)\n",		\
	     __func__, _chan);						\
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
  c1725_address *tmp_c1725;

  if(addr<=21) /* CR-CSR addressing */
    {
#ifdef VXWORKS
      fprintf(stderr, "%s: ERROR: CR-CSR addressing not supported in vxWorks\n",
	     __func__);
      return ERROR;
#else
      AMcode=0x2F;
      addr = addr<<19;
      addr_inc = addr_inc<<19;
      printf("%s: Initializing using CR-CSR (0x%02x)\n",__func__,AMcode);
#endif
    }
  else if (addr < 0xffffff) /* A24 addressing */
    {
      AMcode=0x39;
      printf("%s: Initializing using A24 (0x%02x)\n",__func__,AMcode);
    }
  else /* A32 addressing */
    {
      AMcode=0x09;
      printf("%s: Initializing using A32 (0x%02x)\n",__func__,AMcode);
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
      fprintf(stderr, "%s: ERROR in sysBusToLocalAdrs (0x%02x, 0x%x, &laddr) \n",
	      __func__,AMcode,addr);
#else
      fprintf(stderr, "%s: ERROR in vmeBusToLocalAdrs (0x%02x, 0x%x, &laddr) \n",
	      __func__,AMcode,addr);
#endif
      return ERROR;
    }

  Nc1725 = 0;
  for (i = 0; i < nadc; i++)
    {
      tmp_c1725 = (c1725_address *) (laddr + i * addr_inc);
      /* Check if Board exists at that address */
#ifdef VXWORKS
      res = vxMemProbe ((char *) &tmp_c1725->board_info, VX_READ, 4, (char *) &boardInfo);
#else
      res = vmeMemProbe ((char *) &tmp_c1725->board_info, 4, (char *) &boardInfo);
#endif

      if (res < 0)
	{
	  printf("%s: No addressable board at address = 0x%lx\n",
		  __func__, (unsigned long) tmp_c1725 - c1725AddrOffset);
	  continue;
	}

      /* Check that this is a c1725 */
      boardID = (vmeRead32(&tmp_c1725->rom.board0) |
		 (vmeRead32(&tmp_c1725->rom.board1)<<8));

      if((boardID & C1725_ROM_BOARD_ID_MASK)!=C1725_ROM_BOARD_ID)
	{
	  printf("%s: Invalid board type (0x%x != 0x%x) at address 0x%lx\n",
		 __func__,boardID, C1725_ROM_BOARD_ID,
		 (unsigned long) tmp_c1725 - c1725AddrOffset);
	  continue;
	}
      uint32_t slot_number = vmeRead32(&tmp_c1725->board_id) & C1725_BOARDID_GEO_MASK;
      if(slot_number == 0)
	{
	  printf("%s: Invalid slot number from module (%d).. decoding from address.\n",
		  __func__, slot_number);
	  /* try to form the slot_number from the a24 */
	  slot_number = (uint32_t)((unsigned long) tmp_c1725 - c1725AddrOffset) >> 19;
	  if((slot_number < 2) || (slot_number > MAX_VME_SLOTS))
	    {
	      fprintf(stderr, "%s: ERROR: Unable to get slot number from address (0x%lx)\n",
		      __func__, (unsigned long) tmp_c1725 - c1725AddrOffset);
	      continue;
	    }
	  /* Set it , if we haven't already a 725 with this slot number*/
	  int32_t ic=0, found = 0;
	  for(ic = 0; ic < Nc1725; ic++)
	    {
	      if(c1725ID[ic] == slot_number)
		found = 1;
	    }
	  if(found == 0)
	    {
	      vmeWrite32(&tmp_c1725->board_id, slot_number);
	    }
	  else
	    {
	      fprintf(stderr, "%s: ERROR: slot number (%d) already used by library!\n",
		      __func__, slot_number);
	      continue;
	    }

	}

      c1725p[slot_number] = tmp_c1725;
      c1725ID[Nc1725++] = slot_number;
      printf("%s: Initialized C1792 in slot %d at address 0x%lx \n", __func__,
	      slot_number, (unsigned long) c1725p[slot_number] - c1725AddrOffset);
    }

  if (Nc1725 > 0)
    printf("%s: %d ADC (s) successfully initialized\n", __func__, Nc1725);

  return OK;
}

/**
 * @brief Convert index into a slot number
 * @param[in] i index
 * @return Slot number if sucessful, ERROR otherwise.
 */

int32_t
c1725Slot(int32_t i)
{
  if(i >= Nc1725)
    {
      fprintf(stderr, "%s: ERROR: Index (%d) >= C1725 Initialized (%d)\n",
	      __func__, i, Nc1725);
      return ERROR;
    }

  return c1725ID[i];
}

/**
 * @brief Return mask of initalized module slotnumbers
 * @return Slot number mask if sucessful, ERROR otherwise.
 */

uint32_t
c1725SlotMask()
{
  int32_t ic = 0;
  uint32_t rval = 0;
  if (Nc1725 <=0)
    return -1;

  for(ic = 0; ic < Nc1725; ic++)
    {
      rval |= (1 << c1725ID[ic]);
    }

  return rval;
}

/**
 * @brief Return the number of initialized modules
 * @return number of initialized modules
 */

int32_t
c1725N()
{
  return Nc1725;
}

#if OLDSTATUS
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
  uint32_t firmware, board_info, config;
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
#endif // OLDSTATUS

void
c1725GStatus(int32_t sflag)
{
  int32_t ic = 0, id = 0;

  printf("\n");

  printf("                    -- CAEN1725 Module Configuration Summary --\n");
  printf("\n");
  printf("          Firmware                                                    NEvents\n");
  printf("Slot      Revision  Date      A24       CBLT/MCST Address             BLT\n");
  printf("--------------------------------------------------------------------------------\n");
  /*      |---------|---------|---------|---------|---------|---------|---------|--------- */
  /*       00       22.22     12/12/21  0x123456  0x12345678 - DISABLED         123*/

  for(ic = 0; ic < Nc1725; ic++)
    {
      uint32_t addr = 0, mcst = 0, position = 0;
      uint32_t major = 0, minor = 0, date = 0;
      uint32_t max_events = 0;;

      id = c1725ID[ic];
      addr = (uint32_t)((unsigned long)c1725p[id] - c1725AddrOffset);

      c1725GetMulticast(id, &mcst, &position);
      c1725GetROCFimwareRevision(id, &major, &minor, &date);
      c1725GetMaxEventsPerBLT(id, &max_events);

      printf(" %2d%7s", id, "");

      printf("%2d.%02d%5s",
	     major, minor, "");

      printf("%02d/%02d/%02d%2s",
	     (date & 0xF000) >> 12,
	     (date & 0x0F00) >> 8,
	     (date & 0x00FF) , "");

      printf("0x%06x%2s",
	     addr, "");

      printf("0x%08x - ", mcst);
      printf("%8s%9s",
	     (position == 0) ? "DISABLED" :
	     (position == 1) ? "LAST" :
	     (position == 2) ? "FIRST" :
	     (position == 3) ? "MIDDLE" :
	     "", "");

      printf("%3d", max_events);


      printf("\n");
    }

  printf("\n");
  printf("                    -- Board Config --        \n");
  printf("\n");
  printf("Slot      TRG-IN    VetoLogic FlagTrunc\n");
  printf("--------------------------------------------------------------------------------\n");
  /*      |---------|---------|---------|---------|---------|---------|---------|--------- */
  /*       00       TRIG      LOW       ENABLED */

  for(ic = 0; ic < Nc1725; ic++)
    {
      uint32_t trg_in_mode, veto_polarity, frag_trunc_event;
      id = c1725ID[ic];
      c1725GetBoardConfiguration(id, &trg_in_mode, &veto_polarity, &frag_trunc_event);

      printf(" %2d%7s", id, "");
      printf("%-10.10s", (trg_in_mode == 0) ? "TRIG" : "VETO");
      printf("%-10.10s", (veto_polarity == 1) ? "HIGH" : "LOW");
      printf("%-10.10s", (frag_trunc_event == 1) ? "ENABLED" : "disabled" );

      printf("\n");
    }


  printf("\n");
  printf("                    -- Acquisition Control --\n");
  printf("\n");
  printf("Slot      Mode      Arm       ClkSrc    BUSY      VETO      RUNIN   \n");
  printf("--------------------------------------------------------------------------------\n");
  /*      |---------|---------|---------|---------|---------|---------|---------|--------- */
  /*       00       First     Start     External  ENABLED   ENABLED   ENABLED */

  for(ic = 0; ic < Nc1725; ic++)
    {
      uint32_t mode, arm, clocksource,lvds_busy_enable, lvds_veto_enable, lvds_runin_enable;
      id = c1725ID[ic];

      c1725GetAcquisitionControl(id, &mode, &arm, &clocksource,
				 &lvds_busy_enable, &lvds_veto_enable,
				 &lvds_runin_enable);

      printf(" %2d%7s", id, "");
      printf("%-10.10s",
	     (mode == 0) ? "Soft" :
	     (mode == 1) ? "S-IN" :
	     (mode == 2) ? "First" :
	     (mode == 3) ? "LVDS" : "??");
      printf("%-10.10s",
	     (arm == 1) ? "Start" : "Stop");
      printf("%-10.10s",
	     (clocksource == 1) ? "External" : "Internal");
      printf("%-10.10s",
	     (lvds_busy_enable == 1) ? "ENABLED" : "disabled");
      printf("%-10.10s",
	     (lvds_veto_enable == 1) ? "ENABLED" : "disabled");
      printf("%-10.10s",
	     (lvds_runin_enable == 1) ? "ENABLED" : "disabled");

      printf("\n");
    }

  printf("\n");
  printf("                    -- Acquisition Status -- \n");
  printf("\n");
  printf("                    Event     Event                                   Inp Level\n");
  printf("Slot      Run       Ready     Full      ClockSrc  PLL       Ready     SIN   TRG\n");
  printf("--------------------------------------------------------------------------------\n");
  /*       00       Running   READY     FULL      EXT       lock      Ready     HI    lo   */

  for(ic = 0; ic < Nc1725; ic++)
    {
      uint32_t arm, eventready, eventfull, clocksource, pll, ready, sinlevel,
	trglevel, shutdown, temperature;
      id = c1725ID[ic];
      c1725GetAcquisitionStatus(id, &arm, &eventready,
				&eventfull, &clocksource,
				&pll, &ready, &sinlevel,
				&trglevel, &shutdown, &temperature);

      printf(" %2d%7s", id, "");
      printf("%-10.10s", (arm == 1) ? "Running" : "Stopped");
      printf("%-10.10s", (eventready == 1) ? "READY" : "----");
      printf("%-10.10s", (eventfull == 1) ? "FULL" : "----");
      printf("%-10.10s", (clocksource == 1) ? "EXT" : "INT");
      printf("%-10.10s", (pll == 1) ? "lock" : "*UNLOCK*");
      printf("%-10.10s", (ready == 1) ? "Ready" : "*NOT READY*");
      printf("%-6.2s", (sinlevel == 1) ? "HI" : "lo");
      printf("%2s", (trglevel == 1) ? "HI" : "lo");

      printf("\n");
    }

  printf("\n");
  printf("                    -- Readout Control -- \n");
  printf("\n");
  printf("          VME       Optical   VME                 Address   Int       Extended\n");
  printf("Slot      IntLevel  Int       BERR      Align64   Relocate  Release   BlkSpace \n");
  printf("--------------------------------------------------------------------------------\n");

  for(ic = 0; ic < Nc1725; ic++)
    {
      uint32_t intlevel= 0, optical_int = 0, vme_berr = 0, align64= 0, address_relocate = 0,
	roak = 0, ext_blk_space = 0;

      id = c1725ID[ic];
      c1725GetReadoutControl(id, &intlevel, &optical_int, &vme_berr, &align64,
			     &address_relocate, &roak, &ext_blk_space);

      printf(" %2d%7s", id, "");

      if(intlevel)
	printf("%d%9s", intlevel, "");
      else
	printf("%-10.10s", "disabled");

      printf("%-10.10s", (optical_int == 1) ? "ENABLED" : "disabled");
      printf("%-10.10s", (vme_berr == 1) ? "ENABLED" : "disabled");
      printf("%-10.10s", (align64 == 1) ? "ENABLED" : "disabled");
      printf("%-10.10s", (address_relocate == 1) ? "ENABLED" : "disabled");
      printf("%-10.10s", (roak == 1) ? "ROAK" : "ROAR");
      printf("%-10.10s", (ext_blk_space == 1) ? "ENABLED" : "disabled");

      printf("\n");
    }

  printf("\n");
  printf("                    -- Readout Status -- \n");
  printf("\n");
  printf("          Event     BERR      VME       Events    Event     Board Failure \n");
  printf("Slot      Ready     Flag      FIFO      Stored    Size      PLL  Temp Power\n");
  printf("--------------------------------------------------------------------------------\n");
  /*      |---------|---------|---------|---------|---------|---------|---------|--------- */
  /*       00   */

  for(ic = 0; ic < Nc1725; ic++)
    {
      uint32_t evstored = 0, eventsize = 0, pll = 0, temperature = 0, powerdown = 0,
	event_ready = 0, berr = 0, vme_fifo_empty = 0;
      id = c1725ID[ic];
      c1725GetReadoutStatus(id, &event_ready, &berr, &vme_fifo_empty);
      c1725GetEventSize(id, &eventsize);
      c1725GetEvStored(id, &evstored);
      c1725GetBoardFailureStatus(id, &pll, &temperature, &powerdown);

      printf(" %2d%7s", id, "");

      printf("%-10.10s",
	     event_ready ? "READY" : "-----");

      printf("%-10.10s",
	     berr ? "HIGH" : "low");

      printf("%-10.10s",
	     vme_fifo_empty ? "Empty" : "NotEmpty");

      printf("%9d%1s", evstored, "");
      printf("%9d%1s", eventsize, "");

      printf("%-5.4s",
	     pll ? "FAIL" : "OK");
      printf("%-5.4s",
	     temperature ? "FAIL" : "OK");
      printf("%-5.4s",
	     powerdown ? "FAIL" : "OK");

      printf("\n");
    }

  printf("\n");
  printf("                    -- Global Trigger Enable -- \n");
  printf("\n");
  printf("          Channel   Coinc      Majority    \n");
  printf("Slot      Mask      Window     Level     LVDS      External  Software\n");
  printf("--------------------------------------------------------------------------------\n");
  /*      |---------|---------|---------|---------|---------|---------|---------|--------- */
  /*       00   */

  for(ic = 0; ic < Nc1725; ic++)
    {
      id = c1725ID[ic];
      uint32_t channel_enable = 0, majority_coincidence_window = 0, majority_level = 0,
	lvds_trigger_enable = 0, external_trigger_enable = 0, software_trigger_enable = 0;

      c1725GetGlobalTrigger(id, &channel_enable, &majority_coincidence_window,
			    &majority_level, &lvds_trigger_enable,
			    &external_trigger_enable, &software_trigger_enable);



      printf(" %2d%7s", id, "");

      printf("0x%02x%6s", channel_enable, "");

      printf("%2d%9s", majority_coincidence_window, "");

      printf("%d%9s", majority_level, "");

      printf("%-10.10s", lvds_trigger_enable ? "ENABLED" : "disabled");

      printf("%-10.10s", external_trigger_enable ? "ENABLED" : "disabled");

      printf("%-10.10s", software_trigger_enable ? "ENABLED" : "disabled");

      printf("\n");
    }

  printf("\n");
  printf("                    -- Front Panel TRG-OUT Enable -- \n");
  printf("\n");
  printf("          Channel   Channel   Majority    \n");
  printf("Slot      Mask      Logic     Level     LVDS      External  Software\n");
  printf("--------------------------------------------------------------------------------\n");
  /*      |---------|---------|---------|---------|---------|---------|---------|--------- */
  /*       00   */

  for(ic = 0; ic < Nc1725; ic++)
    {
      id = c1725ID[ic];
      uint32_t channel_enable = 0, channel_logic = 0, majority_level = 0,
	lvds_trigger_enable = 0, external_trigger_enable = 0, software_trigger_enable = 0;

      c1725GetFPTrigOut(id, &channel_enable, &channel_logic,
			&majority_level, &lvds_trigger_enable,
			&external_trigger_enable, &software_trigger_enable);



      printf(" %2d%7s", id, "");

      printf("0x%02x%6s", channel_enable, "");

      printf("%-10.10s",
	     (channel_logic == C1725_FPTRGOUT_CHANNEL_LOGIC_OR) ? "OR" :
	     (channel_logic == C1725_FPTRGOUT_CHANNEL_LOGIC_AND) ? "AND" :
	     (channel_logic == C1725_FPTRGOUT_CHANNEL_LOGIC_MAJORITY) ? " MAJORITY" : "???");

      printf("%d%9s", majority_level, "");

      printf("%-10.10s", lvds_trigger_enable ? "ENABLED" : "disabled");

      printf("%-10.10s", external_trigger_enable ? "ENABLED" : "disabled");

      printf("%-10.10s", software_trigger_enable ? "ENABLED" : "disabled");

      printf("\n");
    }

  printf("\n");
  printf("                    -- Front Panel IO Control -- \n");
  printf("\n");
  printf("                              -          Mode Masks         -\n");
  printf("Slot      LEMO Lvl  TRG-OUT   LVDS      TRG-IN    TRG-OUT\n");
  printf("--------------------------------------------------------------------------------\n");
  /*      |---------|---------|---------|---------|---------|---------|---------|--------- */
  /*       00   */

  for(ic = 0; ic < Nc1725; ic++)
    {
      id = c1725ID[ic];

      uint32_t lemo_level = 0, lemo_enable = 0, lvds_mask = 0, trg_in_mask = 0, trg_out_mask = 0;

      c1725GetFPIO(id, &lemo_level, &lemo_enable, &lvds_mask, &trg_in_mask, &trg_out_mask);

      printf(" %2d%7s", id, "");

      printf("%-10.10s", lemo_level ? "TTL" : "NIM");
      printf("%-10.10s", lemo_enable ? "ENABLED" : "disabled");

      printf("0x%02x%6s", lvds_mask, "");
      printf("0x%x%7s", trg_in_mask, "");
      printf("0x%03x%6s", trg_out_mask, "");

      printf("\n");
    }

  printf("\n");
  printf("    \n");
  printf("          Run       ExtVeto\n");
  printf("Slot      Delay     Delay\n");
  printf("--------------------------------------------------------------------------------\n");
  /*      |---------|---------|---------|---------|---------|---------|---------|--------- */
  /*       00   */

  for(ic = 0; ic < Nc1725; ic++)
    {
      id = c1725ID[ic];

      uint32_t run_delay = 0; uint32_t veto_delay = 0;
      c1725GetRunDelay(id, &run_delay);
      c1725GetExtendedVetoDelay(id, &veto_delay);

      printf(" %2d%7s", id, "");

      printf("%d%10s", run_delay, "");
      printf("%d%10s", veto_delay, "");

      printf("\n");
    }


#ifdef _template_
  printf("\n");
  printf("    \n");
  printf("    \n");
  printf("Slot\n");
  printf("--------------------------------------------------------------------------------\n");
  /*      |---------|---------|---------|---------|---------|---------|---------|--------- */
  /*       00   */

  for(ic = 0; ic < Nc1725; ic++)
    {
      id = c1725ID[ic];

      printf(" %2d%7s", id, "");
      printf("\n");
    }
#endif

  printf("\n");
  printf("--------------------------------------------------------------------------------\n");

  printf("\n");
  printf("\n");

}

int32_t
c1725ChannelsStatus(int32_t id, int32_t sflag)
{
  int32_t ic = 0;
  CHECKID(id);

  printf("\n");
  printf("    \n");
  printf("          Min       Dyn       Input     Pre       Trigger    Fixed    \n");
  printf("Ch        Length    Range     Delay     Trigger   Threshold  Baseline \n");
  printf("--------------------------------------------------------------------------------\n");
  /*      |---------|---------|---------|---------|---------|---------|---------|--------- */
  /*       00   */

  for(ic = 0; ic < C1725_MAX_ADC_CHANNELS; ic++)
    {
      uint32_t min_record_length = 0, range = 0, delay = 0,
	pretrigger = 0, thres = 0, baseline = 0;

      c1725GetRecordLength(id, ic, &min_record_length);
      c1725GetDynamicRange(id, ic, &range);
      c1725GetInputDelay(id, ic, &delay);
      c1725GetPreTrigger(id, ic, &pretrigger);
      c1725GetTriggerThreshold(id, ic, &thres);
      c1725GetFixedBaseline(id, ic, &baseline);

      printf(" %2d%7s", ic, "");

      printf("%7d%3s", min_record_length, "");

      printf("%-10.10s", range ? "0.5 Vpp" : "2 Vpp");

      printf("%3d%7s", delay, "");
      printf("%3d%7s", pretrigger, "");
      printf("%5d%6s", thres, "");
      printf("%5d%5s", baseline, "");

      printf("\n");
    }

  printf("\n");
  printf("                    Samples             Couple    \n");
  printf("          Couple    Under     Max       Over      DC\n");
  printf("Ch        Logic     Threshold Tail      Logic     Offset\n");
  printf("--------------------------------------------------------------------------------\n");
  /*      |---------|---------|---------|---------|---------|---------|---------|--------- */
  /*       00   */

  for(ic = 0; ic < C1725_MAX_ADC_CHANNELS; ic++)
    {
      uint32_t logic = 4, thres = 0, maxtail = 0, over_logic = 4, offset = 0;

      /* c1725GetCoupleTriggerLogic(id, ic, &logic); */
      c1725GetSamplesUnderThreshold(id, ic, &thres);
      c1725GetMaxmimumTail(id, ic, &maxtail);
      /* c1725GetCoupleOverTriggerLogic(id, ic, &over_logic); */

      c1725GetDCOffset(id, ic, &offset);

      printf(" %2d%7s", ic, "");

      printf("%-10.10s",
	     (logic==0) ? "AND" :
	     (logic == 1) ? "EVEN" :
	     (logic == 2) ? "ODD" :
	     (logic == 3) ? "OR" : "??");

      printf("%7d%4s", thres, "");
      printf("%7d%4s", maxtail, "");

      printf("%-10.10s",
	     (over_logic==0) ? "AND" :
	     (over_logic == 1) ? "N" :
	     (over_logic == 2) ? "N+1" :
	     (over_logic == 3) ? "OR" : "??");

      printf("%5d%6s", offset, "");

      printf("\n");
    }

  printf("\n");
  printf("                                                  Self\n");
  printf("          -   Internal Test Pulse    -            Trigger\n");
  printf("Ch        Enable    Rate      Polarity            Enable    \n");
  printf("--------------------------------------------------------------------------------\n");
  /*      |---------|---------|---------|---------|---------|---------|---------|--------- */
  /*       00   */
  for(ic = 0; ic < C1725_MAX_ADC_CHANNELS; ic++)
    {
      uint32_t test_pulse_enable = 0, test_pulse_rate = 0, test_pulse_polarity = 0,
	self_trigger_rate = 0, self_trigger_enable = 0;

      c1725GetDPPControl(id, ic,
			 &test_pulse_enable, &test_pulse_rate,
			 &test_pulse_polarity, &self_trigger_enable);

      printf(" %2d%7s", ic, "");

      printf("%-10.10s", test_pulse_enable ? "ENABLED" : "disabled");

      printf("%-10.10s",
	     (test_pulse_rate == 0) ? "500 Hz" :
	     (test_pulse_rate == 1) ? "5 kHz" :
	     (test_pulse_rate == 2) ? "50 kHz" :
	     (test_pulse_rate == 3) ? "500 kHz" : "??");

      printf("%-10.10s", test_pulse_polarity ? "negative" : "POSITIVE");

      printf("%-10.10s", "");

      printf("%-10.10s", self_trigger_enable ? "ENABLED" : "disabled");

      printf("\n");
    }

  printf("\n");
  printf("    \n");
  printf("    \n");
  printf("Ch        Memory    SPI       Calib     Overtemp            Temp\n");
  printf("--------------------------------------------------------------------------------\n");
  /*      |---------|---------|---------|---------|---------|---------|---------|--------- */
  /*       00   */

  for(ic = 0; ic < C1725_MAX_ADC_CHANNELS; ic++)
    {
      uint32_t memory = 0, spi_busy = 0, calibration = 0, overtemp = 0,
	temperature = 0;

      c1725GetChannelStatus(id, ic, &memory, &spi_busy,
			    &calibration, &overtemp);
      c1725GetADCTemperature(id, ic, &temperature);

      printf(" %2d%7s", ic, "");

      printf("%-10.10s",
	     (memory == 1) ? "FULL" :
	     (memory == 2) ? "empty" :
	     (memory == 0) ? "Not Empty" : "??");

      printf("%-10.10s", spi_busy ? "BUSY" : "ok");

      printf("%-10.10s", calibration ? "DONE" : "NOT done");

      printf("%-10.10s", overtemp ? "POWERDOWN" : "ok");

      printf("%-10.10s", "");

      printf("%3d%8s", (int8_t)temperature, "");


      printf("\n");
    }

#ifdef _template_
  printf("\n");
  printf("    \n");
  printf("    \n");
  printf("Ch\n");
  printf("--------------------------------------------------------------------------------\n");
  /*      |---------|---------|---------|---------|---------|---------|---------|--------- */
  /*       00   */

  for(ic = 0; ic < C1725_MAX_ADC_CHANNELS; ic++)
    {

      printf(" %2d%7s", ic, "");
      printf("\n");
    }
#endif

  printf("\n");
  printf("--------------------------------------------------------------------------------\n");

  printf("\n");
  printf("\n");

  return OK;
}

/**
 * @brief Set the BoardConfiguration
 * @param[in] id caen1725 slot ID
 * @param[in] trg_in_mode External Trigger Mode (0: trigger, 1: veto)
 * @param[in] veto_polarity Veto Polarity (0: low, 1: high)
 * @param[in] flag_trunc_event Flag Truncated Event (1: enabled, 0:disabled)
 * @return OK if successful, ERROR otherwise.
 */
int32_t
c1725SetBoardConfiguration(int32_t id, uint32_t trg_in_mode,
			   uint32_t veto_polarity, uint32_t flag_trunc_event)
{
  uint32_t setbits = 0, clearbits = 0;
  CHECKID(id);

  /* Manual suggests this MUST BE SET */
  setbits = (1 << 4);
  setbits |= C1725_CONFIG_INDIVIDUAL_TRIGGER;

  if(trg_in_mode)
    setbits |= C1725_CONFIG_TRG_IN_VETO;
  else
    clearbits |= C1725_CONFIG_TRG_IN_VETO;

  if(veto_polarity)
    setbits |= C1725_CONFIG_VETO_LEVEL_HI;
  else
    clearbits |= C1725_CONFIG_VETO_LEVEL_HI;

  if(flag_trunc_event) // Note logic flip
    clearbits |= C1725_CONFIG_FLAG_TRUNC_EVENT;
  else
    setbits |= C1725_CONFIG_FLAG_TRUNC_EVENT;

  C1725LOCK;
  if(setbits)
    vmeWrite32(&c1725p[id]->config_bitset, setbits);

  if(clearbits)
    vmeWrite32(&c1725p[id]->config_bitclear, clearbits);
  C1725UNLOCK;

  return OK;
}


/**
 * @brief Get the Board Configuration
 * @param[in] id caen1725 slot ID
 * @param[out] trg_in_mode External Trigger Mode (0: trigger, 1: veto)
 * @param[out] veto_polarity Veto Polarity (0: low, 1: high)
 * @param[out] flag_trunc_event Flag Truncated Event (0: enabled, 1:disabled)
 * @return OK if successful, ERROR otherwise.
 */
int32_t
c1725GetBoardConfiguration(int32_t id, uint32_t *trg_in_mode,
			   uint32_t *veto_polarity, uint32_t *flag_trunc_event)
{
  uint32_t rreg = 0;
  CHECKID(id);

  C1725LOCK;
  rreg = vmeRead32(&c1725p[id]->config);

  *trg_in_mode = (rreg & C1725_CONFIG_TRG_IN_VETO) ? 1 : 0;
  *veto_polarity = (rreg & C1725_CONFIG_VETO_LEVEL_HI) ? 1 : 0;
  *flag_trunc_event = (rreg & C1725_CONFIG_FLAG_TRUNC_EVENT) ? 0 : 1;
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Perform ADC Calibration
 * @param[in] id caen1725 slot ID
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725ADCCalibration(int32_t id)
{
  int32_t iwait=0, maxwait=1000;
  CHECKID(id);

  C1725LOCK;
  vmeWrite32(&c1725p[id]->channel_adc_calibration, 1);

  /* Prescription from the manual */
  while(iwait<maxwait)
    {
      if((vmeRead32(&c1725p[id]->chan[0].status) & C1725_CHANNEL_STATUS_CALIB_DONE)==1)
	break;
      iwait++;
    }
  C1725UNLOCK;

  if(iwait>=maxwait)
    {
      fprintf(stderr, "%s(%d):: ERROR: Timeout in ADC Calibration\n",
	      __func__, id);
      return ERROR;
    }

  return OK;
}

/**
 * @brief Get the acquisition control settings
 * @param[in] id caen1725 slot ID
 * @param[in] mode Start/Stop Mode (0: SW, 1: S-IN, 2: First Trigger, 3: LVDS)
 * @param[in] arm Start (1), Stop (0) acquisition
 * @param[in] clocksource Internal (0), External (1) clock source
 * @param[in] lvds_busy_enable Enable busy LVDS i/o
 * @param[in] lvds_veto_enable Enable veto LVDS i/o
 * @param[in] lvds_runin_enable Enable RunIN LVDS i/o
 * @return OK if successful, ERROR otherwise.
 */
int32_t
c1725SetAcquisitionControl(int32_t id, uint32_t mode, uint32_t arm, uint32_t clocksource,
			   uint32_t lvds_busy_enable, uint32_t lvds_veto_enable,
			   uint32_t lvds_runin_enable)
{
  uint32_t wreg = 0;

  CHECKID(id);

  if(mode > C1725_ACQ_MODE_MASK)
    {
      fprintf(stderr, "%s: ERROR: Invalid mode (%d)\n",
	      __func__, mode);
      return ERROR;
    }

  wreg = mode;

  wreg |= (arm) ? C1725_ACQ_RUN : 0;
  wreg |= (clocksource) ? C1725_ACQ_CLK_EXT : 0;
  wreg |= (lvds_busy_enable) ? C1725_ACQ_LVDS_BUSY_ENABLE : 0;
  wreg |= (lvds_veto_enable) ? C1725_ACQ_LVDS_VETO_ENABLE : 0;
  wreg |= (lvds_runin_enable) ? C1725_ACQ_LVDS_RUNIN_ENABLE : 0;

  C1725LOCK;
  vmeWrite32(&c1725p[id]->acq_ctrl, wreg);
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Get the acquisition control settings
 * @param[in] id caen1725 slot ID
 * @param[out] mode Start/Stop Mode (0: SW, 1: S-IN, 2: First Trigger, 3: LVDS)
 * @param[out] arm Start (1), Stop (0) acquisition
 * @param[out] clocksource Internal (0), External (1) clock source
 * @param[out] lvds_busy_enable Enable busy LVDS i/o
 * @param[out] lvds_veto_enable Enable veto LVDS i/o
 * @param[out] lvds_runin_enable Enable RunIN LVDS i/o
 * @return OK if successful, ERROR otherwise.
 */
int32_t
c1725GetAcquisitionControl(int32_t id, uint32_t *mode, uint32_t *arm, uint32_t *clocksource,
			   uint32_t *lvds_busy_enable, uint32_t *lvds_veto_enable,
			   uint32_t *lvds_runin_enable)
{
  uint32_t rreg = 0;

  CHECKID(id);


  C1725LOCK;
  rreg = vmeRead32(&c1725p[id]->acq_ctrl);

  *mode = (rreg & C1725_ACQ_MODE_MASK);
  *arm  = (rreg & C1725_ACQ_RUN) ? 1 : 0;
  *clocksource = (rreg & C1725_ACQ_CLK_EXT) ? 1 : 0;
  *lvds_busy_enable  = (rreg & C1725_ACQ_LVDS_BUSY_ENABLE) ? 1 : 0;
  *lvds_veto_enable  = (rreg & C1725_ACQ_LVDS_VETO_ENABLE) ? 1 : 0;
  *lvds_runin_enable  = (rreg & C1725_ACQ_LVDS_RUNIN_ENABLE) ? 1 : 0;

  C1725UNLOCK;

  return OK;
}

/**
 * @brief Get the acquisition control status
 * @param[in] id caen1725 slot ID
 * @param[out] arm Aquisition Running (1) or Stopped (0)
 * @param[out] eventready At least one event ready for readout (1)
 * @param[out] eventfull Max number of events to be read has been reached (1)
 * @param[out] clocksource Internal (0), External (1) clock source
 * @param[out] pll PLL Locked (1), Unlock condition occurred (0)
 * @param[out] ready Board ready to start acqusition (1)
 * @param[out] sinlevel S-IN logic level
 * @param[out] trglevel TRG-IN logic level
 * @param[out] shutdown Channels are in shutdown (1)
 * @param[out] temperature Temperature status of board mezzanines
 * @return OK if successful, ERROR otherwise.
 */
int32_t
c1725GetAcquisitionStatus(int32_t id, uint32_t *arm, uint32_t *eventready,
			  uint32_t *eventfull, uint32_t *clocksource,
			  uint32_t *pll, uint32_t *ready, uint32_t *sinlevel,
			  uint32_t *trglevel, uint32_t *shutdown, uint32_t *temperature)
{
  uint32_t rreg = 0;

  CHECKID(id);


  C1725LOCK;
  rreg = vmeRead32(&c1725p[id]->acq_status);

  *arm  = (rreg & C1725_ACQ_RUN) ? 1 : 0;
  *eventready = (rreg & C1725_ACQ_STATUS_EVENT_READY) ? 1 : 0;
  *clocksource = (rreg & C1725_ACQ_STATUS_CLK_EXTERNAL) ? 1 : 0;
  *pll  = (rreg & C1725_ACQ_STATUS_PLL_LOCKED) ? 1 : 0;
  *ready  = (rreg & C1725_ACQ_STATUS_ACQ_READY) ? 1 : 0;
  *sinlevel  = (rreg & C1725_ACQ_STATUS_SINLEVEL) ? 1 : 0;
  *trglevel =  (rreg & C1725_ACQ_STATUS_TRGLEVEL) ? 1 : 0;
  *shutdown =  (rreg & C1725_ACQ_STATUS_SHUTDOWN) ? 1 : 0;
  *temperature =  (rreg & C1725_ACQ_STATUS_TEMP_MASK) >> 20;

  C1725UNLOCK;

  return OK;
}


/**
 * @brief Generate a software trigger.
 * @param[in] id caen1725 slot ID
 * @return OK if successful, ERROR otherwise.
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


/**
 * @brief Set which signals contribute to the global trigger generation
 * @param[in] id Slot ID
 * @param[in] channel_enable Mask of channel couples trigger request
 * @param[in] majority_coincidence_window Time window for the majority coincidence
 * @param[in] majority_level Majority level for channel couple logic
 * @param[in] lvds_trigger_enable Enable LVDS connectors programmed as inputs
 * @param[in] external_trigger_enable Enable external TRG-IN
 * @param[in] software_trigger_enable Enable Software trigger
 */

int32_t
c1725SetGlobalTrigger(int32_t id, uint32_t channel_enable,
		      uint32_t majority_coincidence_window, uint32_t majority_level,
		      uint32_t lvds_trigger_enable, uint32_t external_trigger_enable,
		      uint32_t software_trigger_enable)
{
  uint32_t enablebits = 0;
  CHECKID(id);

  if(channel_enable > C1725_GLOBAL_TRG_CHANNEL_MASK)
    {
      fprintf(stderr, "%s: ERROR: Invalid Channel Enable Mask (0x%x)\n",
	      __func__, channel_enable);
      return ERROR;
    }

  enablebits = channel_enable;

  if(majority_coincidence_window > 0xF)
    {
      fprintf(stderr, "%s: ERROR: Invalid Majority Coincidence Window (%d)\n",
	      __func__, majority_coincidence_window);
      return ERROR;
    }

  enablebits |= (majority_coincidence_window << 20);

  if(majority_level > 7)
    {
      fprintf(stderr, "%s: ERROR: Invalid Channel Majority Level (%d)\n",
	      __func__, majority_level);
      return ERROR;
    }

  enablebits |= (majority_level << 24);

  enablebits |= lvds_trigger_enable ? C1725_GLOBAL_TRG_LVDS_ENABLE : 0;
  enablebits |= external_trigger_enable ? C1725_GLOBAL_TRG_EXTERNAL_ENABLE : 0;
  enablebits |= software_trigger_enable ? C1725_GLOBAL_TRG_SOFTWARE_ENABLE : 0;



  C1725LOCK;
  vmeWrite32(&c1725p[id]->global_trigger_mask, enablebits);
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Get which signals contribute to the global trigger generation
 * @param[in] id Slot ID
 * @param[out] channel_enable Mask of channel couples trigger request
 * @param[out] majority_coincidence_window Time window for the majority coincidence
 * @param[out] majority_level Majority level for channel couple logic
 * @param[out] lvds_trigger_enable Enable LVDS connectors programmed as inputs
 * @param[out] external_trigger_enable Enable external TRG-IN
 * @param[out] software_trigger_enable Enable Software trigger
 */

int32_t
c1725GetGlobalTrigger(int32_t id, uint32_t *channel_enable,
		      uint32_t *majority_coincidence_window, uint32_t *majority_level,
		      uint32_t *lvds_trigger_enable, uint32_t *external_trigger_enable,
		      uint32_t *software_trigger_enable)
{
  uint32_t rval = 0;
  CHECKID(id);


  C1725LOCK;
  rval = vmeRead32(&c1725p[id]->global_trigger_mask);

  *channel_enable = rval & C1725_GLOBAL_TRG_CHANNEL_MASK;
  *majority_coincidence_window = (rval & C1725_GLOBAL_TRG_CHANNEL_COIN_WINDOW_MASK) >> 20;
  *majority_level = (rval & C1725_GLOBAL_TRG_CHANNEL_MAJORITY_LEVEL_MASK) >> 24;

  *lvds_trigger_enable = (rval & C1725_GLOBAL_TRG_LVDS_ENABLE) ? 1 : 0;
  *external_trigger_enable  = (rval & C1725_GLOBAL_TRG_EXTERNAL_ENABLE) ? 1 : 0;
  *software_trigger_enable = (rval & C1725_GLOBAL_TRG_SOFTWARE_ENABLE) ? 1 : 0;

  C1725UNLOCK;

  return OK;
}


/**
 * @brief Set which signals can contribute to generate the signal on the front panel lemo connector
 * @param[in] id Slot id
 * @param[in] channel_enable Mask of channel couples trigger request
 * @param[in] channel_logic Logic of channel couples (0: OR, 1: AND, 2: Majority)
 * @param[in] majority_level Majority level for channel couple logic
 * @param[in] lvds_trigger_enable Enable LVDS connectors programmed as inputs
 * @param[in] external_trigger_enable Enable external TRG-IN
 * @param[in] software_trigger_enable Enable Software trigger
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725SetFPTrigOut(int32_t id, uint32_t channel_enable, uint32_t channel_logic,
		  uint32_t majority_level, uint32_t lvds_trigger_enable,
		  uint32_t external_trigger_enable, uint32_t software_trigger_enable)
{
  int32_t enablebits=0;
  CHECKID(id);

  if(channel_enable > C1725_FPTRGOUT_CHANNEL_MASK)
    {
      fprintf(stderr, "%s: ERROR: Invalid Channel Enable Mask (0x%x)\n",
	      __func__, channel_enable);
      return ERROR;
    }

  enablebits = channel_enable;

  if(channel_logic > 2)
    {
      fprintf(stderr, "%s: ERROR: Invalid Channel Logic (0x%x)\n",
	      __func__, channel_logic);
      return ERROR;
    }

  enablebits |= (channel_logic << 8);

  if(majority_level > 7)
    {
      fprintf(stderr, "%s: ERROR: Invalid Channel Majority Level (%d)\n",
	      __func__, majority_level);
      return ERROR;
    }

  enablebits |= (majority_level << 10);

  enablebits |= lvds_trigger_enable ? C1725_FPTRGOUT_LVDS_ENABLE : 0;
  enablebits |= external_trigger_enable ? C1725_FPTRGOUT_EXTERNAL_ENABLE : 0;
  enablebits |= software_trigger_enable ? C1725_FPTRGOUT_SOFTWARE_ENABLE : 0;

  C1725LOCK;
  vmeWrite32(&c1725p[id]->fp_trg_out_enable_mask, enablebits);
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Set which signals can contribute to generate the signal on the front panel lemo connector
 * @param[in] id Slot id
 * @param[out] channel_enable Mask of channel couples trigger request
 * @param[out] channel_logic Logic of channel couples (0: OR, 1: AND, 2: Majority)
 * @param[out] majority_level Majority level for channel couple logic
 * @param[out] lvds_trigger_enable Enable LVDS connectors programmed as inputs
 * @param[out] external_trigger_enable Enable external TRG-IN
 * @param[out] software_trigger_enable Enable Software trigger
 * @return OK if successful, ERROR otherwise.
 */
int32_t
c1725GetFPTrigOut(int32_t id, uint32_t *channel_enable, uint32_t *channel_logic,
		  uint32_t *majority_level, uint32_t *lvds_trigger_enable,
		  uint32_t *external_trigger_enable, uint32_t *software_trigger_enable)
{
  uint32_t rval = 0;
  CHECKID(id);

  C1725LOCK;
  rval = vmeRead32(&c1725p[id]->fp_trg_out_enable_mask);

  *channel_enable = rval & C1725_FPTRGOUT_CHANNEL_MASK;
  *channel_logic = (rval & C1725_FPTRGOUT_CHANNEL_LOGIC_MASK) >> 8;
  *majority_level = (rval & C1725_FPTRGOUT_CHANNEL_MAJORITY_LEVEL_MASK) >> 10;

  *lvds_trigger_enable = (rval & C1725_FPTRGOUT_LVDS_ENABLE) ? 1 : 0;
  *external_trigger_enable  = (rval & C1725_FPTRGOUT_EXTERNAL_ENABLE) ? 1 : 0;
  *software_trigger_enable = (rval & C1725_FPTRGOUT_SOFTWARE_ENABLE) ? 1 : 0;
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Set the Front Panel IO connectors
 * @param[in] id Slot id
 * @param[in] lemo_level LEMO Electrical Level (0: NIM, 1: TTL)
 * @param[in] lemo_enable TRG-OUT Enable
 * @param[in] lvds_mask LVDS IO mode mask
 * @param[in] trg_in_mask TRG-IN mode mask
 * @param[in] trg_out_mask TRG-OUT mode mask
 * @return OK if successful, ERROR otherwise.
 */
int32_t
c1725SetFPIO(int32_t id, uint32_t lemo_level, uint32_t lemo_enable,
	     uint32_t lvds_mask, uint32_t trg_in_mask, uint32_t trg_out_mask)
{
  uint32_t enablebits = 0;
  CHECKID(id);

  enablebits = lemo_level ? C1725_FPIO_LEMO_LEVEL_TTL : 0;
  enablebits |= lemo_enable ? C1725_FPIO_TRGOUT_ENABLE : 0;

  if(lvds_mask > 0xFF)
    {
      fprintf(stderr, "%s: ERROR: Invalid lvds_mask (0x%x)\n",
	      __func__, lvds_mask);
      return ERROR;
    }

  enablebits |= (lvds_mask << 2);

  if(trg_in_mask > 0x3)
    {
      fprintf(stderr, "%s: ERROR: Invalid trg_in_mask (0x%x)\n",
	      __func__, trg_in_mask);
      return ERROR;
    }

  enablebits |= (trg_in_mask << 10);

  if(trg_out_mask > 0x1FF)
    {
      fprintf(stderr, "%s: ERROR: Invalid trg_out_mask (0x%x)\n",
	      __func__, trg_out_mask);
      return ERROR;
    }

  enablebits |= (trg_out_mask << 14);

  C1725LOCK;
  vmeWrite32(&c1725p[id]->fp_io_ctrl, enablebits);
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Get the setting of the Front Panel IO connectors
 * @param[in] id Slot id
 * @param[out] lemo_level LEMO Electrical Level (0: NIM, 1: TTL)
 * @param[out] lemo_enable TRG-OUT Enable
 * @param[out] lvds_mask LVDS IO mode mask
 * @param[out] trg_in_mask TRG-IN mode mask
 * @param[out] trg_out_mask TRG-OUT mode mask
 * @return OK if successful, ERROR otherwise.
 */
int32_t
c1725GetFPIO(int32_t id, uint32_t *lemo_level, uint32_t *lemo_enable,
	     uint32_t *lvds_mask, uint32_t *trg_in_mask, uint32_t *trg_out_mask)
{
  uint32_t rval = 0;
  CHECKID(id);

  C1725LOCK;
  rval = vmeRead32(&c1725p[id]->fp_io_ctrl);

  *lemo_level = (rval & C1725_FPIO_LEMO_LEVEL_TTL) ? 1 : 0;
  *lemo_enable = (rval & C1725_FPIO_TRGOUT_ENABLE) ? 1 : 0;

  *lvds_mask = (rval & C1725_FPIO_LVDS_MODE_MASK) >> 2;
  *trg_in_mask = (rval & C1725_FPIO_TRGIN_MODE_MASK) >> 10;
  *trg_out_mask = (rval & C1725_FPIO_TRGOUT_MODE_MASK) >> 14;

  C1725UNLOCK;

  return OK;
}


/**
 * @brief Get the ROC firmware revision
 * @param[in] id caen1725 slot ID
 * @param[out] major Major revision
 * @param[out] minor Minor revision
 * @param[out] date Date (0xYMDD)
 * @return OK if successful, ERROR otherwise.
 */
int32_t
c1725GetROCFimwareRevision(int32_t id, uint32_t *major, uint32_t *minor, uint32_t *date)
{
  uint32_t rreg = 0;
  CHECKID(id);

  C1725LOCK;
  rreg = vmeRead32(&c1725p[id]->roc_firmware_revision);

  *major = (rreg & C1725_ROC_FIRMWARE_MAJOR_MASK) >> 8;
  *minor = (rreg & C1725_ROC_FIRMWARE_MINOR_MASK);
  *date = (rreg & C1725_ROC_FIRMWARE_DATE_MASK) >> 16;

  C1725UNLOCK;

  return OK;
}

/**
 * @brief Set the Enable Channel Mask
 * @param[in] id caen1725 slot ID
 * @param[in] chanmask Mask of enabled channels
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725SetEnableChannelMask(int32_t id, uint32_t chanmask)
{
  CHECKID(id);

  if(chanmask>C1725_ENABLE_CHANNEL_MASK)
    {
      fprintf(stderr, "%s: ERROR: Invalid channel mask (0x%x)\n",
	     __func__,chanmask);
      return ERROR;
    }

  C1725LOCK;
  vmeWrite32(&c1725p[id]->channel_enable_mask,chanmask);
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Get the Enable Channel Mask
 * @param[in] id caen1725 slot ID
 * @param[out] chanmask Mask of enabled channels
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725GetEnableChannelMask(int32_t id, uint32_t *chanmask)
{
  CHECKID(id);

  C1725LOCK;
  *chanmask = vmeRead32(&c1725p[id]->channel_enable_mask) & C1725_ENABLE_CHANNEL_MASK;
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Set the signal propogation compensation for the run start / stop signal
 * @param[in] id caen1725 slot ID
 * @param[in] run_delay Signal delay compensation for signal propogation (units of 32ns for 725)
 * @return OK if successful, ERROR otherwise.
 */
int32_t
c1725SetRunDelay(int32_t id, uint32_t run_delay)
{
  CHECKID(id);

  if(run_delay > 0xFF)
    {
      fprintf(stderr, "%s: ERROR: Invalid run_delay (%d)\n",
	      __func__, run_delay);
      return ERROR;

    }

  C1725LOCK;
  vmeWrite32(&c1725p[id]->run_start_stop_delay, run_delay);
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Get the signal propogation compensation for the run start / stop signal
 * @param[in] id caen1725 slot ID
 * @param[out] run_delay Signal delay compensation for signal propogation (units of 32ns for 725)
 * @return OK if successful, ERROR otherwise.
 */
int32_t
c1725GetRunDelay(int32_t id, uint32_t *run_delay)
{
  CHECKID(id);

  C1725LOCK;
  *run_delay = vmeRead32(&c1725p[id]->run_start_stop_delay) & C1725_RUNDELAY_MASK;
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Set the duration of the extended veto for trigger inhibit on TRG-OUT
 * @param[in] id caen1725 slot ID
 * @param[in] veto_delay Extended veto delay, units of 16ns for 725
 * @return OK if successful, ERROR otherwise.
 */
int32_t
c1725SetExtendedVetoDelay(int32_t id, uint32_t veto_delay)
{
  CHECKID(id);

  if(veto_delay > 0xff)
    {
      fprintf(stderr, "%s: ERROR: Invalid veto_delay (%d)\n",
	      __func__, veto_delay);
      return ERROR;

    }

  C1725LOCK;
  vmeWrite32(&c1725p[id]->extended_veto_delay, veto_delay);
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Get the duration of the extended veto for trigger inhibit on TRG-OUT
 * @param[in] id caen1725 slot ID
 * @param[out] veto_delay Extended veto delay, units of 16ns for 725
 * @return OK if successful, ERROR otherwise.
 */
int32_t
c1725GetExtendedVetoDelay(int32_t id, uint32_t *veto_delay)
{
  CHECKID(id);

  C1725LOCK;
  *veto_delay = vmeRead32(&c1725p[id]->extended_veto_delay) & C1725_EXTENDED_VETO_MASK;
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Obtain the number of 32bit words in the next event.
 * @param[in] id caen1725 slot ID
 * @param[out] eventsize The size of event
 * @return OK if successful, ERROR otherwise.
 */
int32_t
c1725GetEventSize(int32_t id, uint32_t *eventsize)
{
  CHECKID(id);

  C1725LOCK;
  *eventsize = vmeRead32(&c1725p[id]->event_size);
  C1725UNLOCK;

  return OK;
}


/**
 * @brief Obtain the number of events current stored in the output buffer
 * @param[in] id caen1725 slot ID
 * @param[out] evstored number of events stored
 * @return OK if successful, ERROR otherwise.
 */
int32_t
c1725GetEvStored(int32_t id, uint32_t *evstored)
{
  CHECKID(id);

  C1725LOCK;
  *evstored = vmeRead32(&c1725p[id]->event_stored);
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
      fprintf(stderr, "%s: ERROR: Invalid dac (%d)\n",
	     __func__,dac);
      return ERROR;
    }

  C1725LOCK;
  vmeWrite32(&c1725p[id]->voltage_level_mode_config, dac);
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
      fprintf(stderr, "%s: ERROR: Invalid mode (%d)\n",
	     __func__,mode);
      return ERROR;
    }

  C1725LOCK;
  vmeWrite32(&c1725p[id]->analog_monitor_mode, mode);
  C1725UNLOCK;

  return OK;
}




/**
 * @brief Get the board failure status
 * @param[in] id caen1725 slot ID
 * @param[out] pll PLL Lock Loss occurred (1)
 * @param[out] temperature Temperature Failure occurred (1)
 * @param[out] powerdown ADC Power Down occurred (1)
 * @return OK if successful, ERROR otherwise.
 */
int32_t
c1725GetBoardFailureStatus(int32_t id, uint32_t *pll, uint32_t *temperature, uint32_t *powerdown)
{
  uint32_t rreg = 0;
  CHECKID(id);

  C1725LOCK;
  rreg = vmeRead32(&c1725p[id]->board_failure_status);

  *pll = (rreg & C1725_BOARD_FAILURE_PLL_LOCK_LOST) ? 1 : 0;
  *temperature = (rreg & C1725_BOARD_FAILURE_OVER_TEMP) ? 1 : 0;
  *powerdown = (rreg & C1725_BOARD_FAILURE_POWER_DOWN) ? 1 : 0 ;
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Set the Readout Control
 * @param[in] id caen1725 slot ID
 * @param[in] intlevel VME Interrupt Level
 * @param[in] optical_int Optical Link Interrupt Enable
 * @param[in] vme_berr VME Bus Error / Event Aligned Readout Enable
 * @param[in] align64 64-bit aligned readout mode Enable
 * @param[in] address_relocate Address Relocation Enable
 * @param[in] roak ROAK (1) or RORA (0) interrupt release mode
 * @param[in] ext_blk_space Extended Block Transfer Space enable
 * @return OK if successful, ERROR otherwise.
 */
int32_t
c1725SetReadoutControl(int32_t id, uint32_t intlevel, uint32_t optical_int,
		       uint32_t vme_berr, uint32_t align64, uint32_t address_relocate,
		       uint32_t roak, uint32_t ext_blk_space)
{
  uint32_t wreg = 0;
  CHECKID(id);

  if(intlevel > C1725_READOUT_CTRL_INTLEVEL_MASK)
    {
      fprintf(stderr, "%s: ERROR: Invalid intlevel (%d)\n",
	      __func__, intlevel);
      return ERROR;

    }

  wreg = intlevel;
  wreg |= optical_int ? C1725_READOUT_CTRL_OPTICAL_INT_ENABLE : 0;
  wreg |= vme_berr ? C1725_READOUT_CTRL_BERR_ENABLE : 0;
  wreg |= align64 ? C1725_READOUT_CTRL_ALIGN64_ENABLE : 0;
  wreg |= address_relocate ? C1725_READOUT_CTRL_RELOC_ENABLE : 0;
  wreg |= roak ? C1725_READOUT_CTRL_ROAK_ENABLE : 0;
  wreg |= ext_blk_space ? C1725_READOUT_CTRL_EXT_BLK_SPACE_ENABLE : 0;

  C1725LOCK;
  vmeWrite32(&c1725p[id]->readout_ctrl, wreg);
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Get the Readout Control
 * @param[in] id caen1725 slot ID
 * @param[out] intlevel VME Interrupt Level
 * @param[out] optical_int Optical Link Interrupt Enable
 * @param[out] vme_berr VME Bus Error / Event Aligned Readout Enable
 * @param[out] align64 64-bit aligned readout mode Enable
 * @param[out] address_relocate Address Relocation Enable
 * @param[out] roak ROAK (1) or RORA (0) interrupt release mode
 * @param[out] ext_blk_space Extended Block Transfer Space enable
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725GetReadoutControl(int32_t id, uint32_t *intlevel, uint32_t *optical_int,
		       uint32_t *vme_berr, uint32_t *align64, uint32_t *address_relocate,
		       uint32_t *roak, uint32_t *ext_blk_space)
{
  uint32_t rreg = 0;
  CHECKID(id);

  C1725LOCK;
  rreg = vmeRead32(&c1725p[id]->readout_ctrl);

  *intlevel = (rreg & C1725_READOUT_CTRL_INTLEVEL_MASK);
  *optical_int = (rreg & C1725_READOUT_CTRL_OPTICAL_INT_ENABLE) ? 1 : 0;
  *vme_berr = (rreg & C1725_READOUT_CTRL_BERR_ENABLE) ? 1 : 0;
  *align64 = (rreg & C1725_READOUT_CTRL_ALIGN64_ENABLE) ? 1 : 0;
  *address_relocate = (rreg & C1725_READOUT_CTRL_RELOC_ENABLE) ? 1 : 0;
  *roak = (rreg & C1725_READOUT_CTRL_ROAK_ENABLE) ? 1 : 0;
  *ext_blk_space = (rreg & C1725_READOUT_CTRL_EXT_BLK_SPACE_ENABLE) ? 1 : 0;

  C1725UNLOCK;

  return OK;
}

/**
 * @brief Summary
 * @param[in] id caen1725 slot ID
 * @param[out] event_ready Event readout for readout
 * @param[out] berr VME Bus Error occurred
 * @param[out] vme_fifo_empty VME FIFO is Empty
 * @return OK if successful, ERROR otherwise.
 */
int32_t
c1725GetReadoutStatus(int32_t id, uint32_t *event_ready, uint32_t *berr, uint32_t *vme_fifo_empty)
{
  uint32_t rreg = 0;
  CHECKID(id);

  C1725LOCK;
  rreg = vmeRead32(&c1725p[id]->readout_status);

  *event_ready = (rreg & C1725_READOUT_STATUS_EVENT_READY) ? 1 : 0;
  *berr = (rreg & C1725_READOUT_STATUS_BERR_OCCURRED) ? 1 : 0;
  *vme_fifo_empty = (rreg & C1725_READOUT_STATUS_VME_FIFO_EMPTY) ? 1 : 0;
  C1725UNLOCK;

  return OK;
}


/**
 * @brief Set multicast / cblt address for all initialized modules
 * @param[in] baseaddr A32 Multicast / CBLT address
 * @return OK if successful, ERROR otherwise.
 */
int32_t
c1725SetMulticast(uint32_t baseaddr)
{
  unsigned long laddr=0;
  int32_t res, ii;

  if(baseaddr == 0)
    {
      baseaddr = c1725MCSTBase;
    }

  if(baseaddr & 0x00FFFFFF)
    {
      printf("%s: WARN: Invalid bits in baseaddr (0x%08x) ignored!\n",
	     __func__, baseaddr);
      baseaddr &= 0xFF000000;
    }

#ifdef VXWORKS
  res = sysBusToLocalAdrs(0x09,(char *) baseaddr,(char **)&laddr);
  if (res != 0)
    {
      fprintf(stderr, "%s: ERROR in sysBusToLocalAdrs(0x09,0x%x,&laddr) \n",
	     __func__, baseaddr);
      return(ERROR);
    }
#else
  res = vmeBusToLocalAdrs(0x09,(char *)(unsigned long)baseaddr,(char **)&laddr);
  if (res != 0)
    {
      fprintf(stderr, "%s: ERROR in vmeBusToLocalAdrs(0x09,0x%x,&laddr) \n",
	     __func__,(baseaddr<<24));
      return(ERROR);
    }
#endif
  c1725MCSTOffset = laddr - baseaddr;

  c1725MCSTp = (c1725_address *)(laddr);

  printf("%s: MCST VME (Local) base address 0x%08lx (0x%lx):\n",
	 __func__,
	 (unsigned long)c1725MCSTp - (unsigned long)c1725MCSTOffset,
	 (unsigned long)c1725MCSTp);

  for(ii = 0; ii < Nc1725; ii++)
    {
      uint32_t wreg = (baseaddr >> 24);
      int32_t id = c1725ID[ii];

      if(ii==0)
	{
	  wreg |= C1725_MCST_SLOT_FIRST;
	  printf("\tFirst  board at 0x%08lx\n",(unsigned long)c1725p[id] - c1725AddrOffset);
	}
      else if (ii == (Nc1725 - 1))
	{
	  wreg |= C1725_MCST_SLOT_LAST;
	  printf("\tLast   board at 0x%08lx\n",(unsigned long)c1725p[id] - c1725AddrOffset);
	}
      else
	{
	  wreg |= C1725_MCST_SLOT_MIDDLE;
	  printf("\tMiddle board at 0x%08lx\n",(unsigned long)c1725p[id] - c1725AddrOffset);
	}

      C1725LOCK;
      vmeWrite32(&c1725p[id]->multicast_address, wreg);
      C1725UNLOCK;

    }

  return(OK);
}

/**
 * @brief Get the multicast address settings of the specified module
 * @param[in] id caen1725 slot ID
 * @param[out] addr A32 multicast Address
 * @param[out] position Position of module in daisy chain (0: disabled, 1: last, 2: first, 3: middle)
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725GetMulticast(int32_t id, uint32_t *addr, uint32_t *position)
{
  uint32_t rreg = 0;
  CHECKID(id);

  C1725LOCK;
  rreg = vmeRead32(&c1725p[id]->multicast_address);

  *addr = (rreg & C1725_MCST_ADDR_MASK) << 24;
  *position = (rreg & C1725_MCST_SLOT_MASK) >> 8;
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Set the maximum number of events transfered for each block transfer
 * @param[in] id caen1725 slot ID
 * @param[in] max_events Max number of events per BLT
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725SetMaxEventsPerBLT(int32_t id, uint32_t max_events)
{
  CHECKID(id);

  if(max_events > C1725_MAX_EVT_BLT_MASK)
    {
      fprintf(stderr, "%s: ERROR: Invalid max_events (%d)\n",
	      __func__, max_events);
      return ERROR;

    }

  C1725LOCK;
  vmeWrite32(&c1725p[id]->max_events_per_blt, max_events);
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Get the maximum number of events transfered for each block transfer
 * @param[in] id caen1725 slot ID
 * @param[out] max_events Max number of events per BLT
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725GetMaxEventsPerBLT(int32_t id, uint32_t *max_events)
{
  CHECKID(id);

  C1725LOCK;
  *max_events = vmeRead32(&c1725p[id]->max_events_per_blt) & C1725_MAX_EVT_BLT_MASK;
  C1725UNLOCK;

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

  return OK;

}

/**
 * @brief Set the Minimum Record Length for the specified channel
 * @param[in] id caen1725 slot ID
 * @param[in] chan Channel Number
 * @param[in] min_record_length
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725SetRecordLength(int32_t id, int32_t chan, uint32_t min_record_length)
{
  CHECKID(id);
  CHECKCHAN(chan);

  if(min_record_length > C1725_RECORD_LENGTH_MASK  )
    {
      fprintf(stderr, "%s: ERROR: Invalid min_record_length (%d)\n",
	      __func__, min_record_length);
      return ERROR;

    }

  C1725LOCK;
  vmeWrite32(&c1725p[id]->chan[chan].minimum_record_length, min_record_length);
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Get the Minimum Record Length for the specified channel
 * @param[in] id caen1725 slot ID
 * @param[in] chan Channel Number
 * @param[out] min_record_length
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725GetRecordLength(int32_t id, int32_t chan, uint32_t *min_record_length)
{
  CHECKID(id);
  CHECKCHAN(chan);

  C1725LOCK;
  *min_record_length = vmeRead32(&c1725p[id]->chan[chan].minimum_record_length) & C1725_RECORD_LENGTH_MASK;
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Set the DynamicRange for the specified channel
 * @param[in] id caen1725 slot ID
 * @param[in] chan Channel Number
 * @param[in] range
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725SetDynamicRange(int32_t id, int32_t chan, uint32_t range)
{
  CHECKID(id);
  CHECKCHAN(chan);

  if(range > C1725_DYNAMIC_RANGE_MASK)
    {
      fprintf(stderr, "%s: ERROR: Invalid range (%d)\n",
	      __func__, range);
      return ERROR;

    }

  C1725LOCK;
  vmeWrite32(&c1725p[id]->chan[chan].input_dynamic_range, range);
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Get the DynamicRange for the specified channel
 * @param[in] id caen1725 slot ID
 * @param[in] chan Channel Number
 * @param[out] range
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725GetDynamicRange(int32_t id, int32_t chan, uint32_t *range)
{
  CHECKID(id);
  CHECKCHAN(chan);

  C1725LOCK;
  *range = vmeRead32(&c1725p[id]->chan[chan].input_dynamic_range) & C1725_DYNAMIC_RANGE_MASK;
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Set the InputDelay for the specified channel
 * @param[in] id caen1725 slot ID
 * @param[in] chan Channel Number
 * @param[in] delay
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725SetInputDelay(int32_t id, int32_t chan, uint32_t delay)
{
  CHECKID(id);
  CHECKCHAN(chan);

  if(delay > C1725_INPUT_DELAY_MASK)
    {
      fprintf(stderr, "%s: ERROR: Invalid delay (%d)\n",
	      __func__, delay);
      return ERROR;

    }

  C1725LOCK;
  vmeWrite32(&c1725p[id]->chan[chan].input_delay, delay);
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Get the InputDelay for the specified channel
 * @param[in] id caen1725 slot ID
 * @param[in] chan Channel Number
 * @param[out] delay
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725GetInputDelay(int32_t id, int32_t chan, uint32_t *delay)
{
  CHECKID(id);
  CHECKCHAN(chan);

  C1725LOCK;
  *delay = vmeRead32(&c1725p[id]->chan[chan].input_delay) & C1725_INPUT_DELAY_MASK;
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Set the PreTrigger for the specified channel
 * @param[in] id caen1725 slot ID
 * @param[in] chan Channel Number
 * @param[in] pretrigger
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725SetPreTrigger(int32_t id, int32_t chan, uint32_t pretrigger)
{
  CHECKID(id);
  CHECKCHAN(chan);

  if(pretrigger > C1725_PRE_TRIGGER_MASK)
    {
      fprintf(stderr, "%s: ERROR: Invalid pretrigger (%d)\n",
	      __func__, pretrigger);
      return ERROR;

    }

  C1725LOCK;
  vmeWrite32(&c1725p[id]->chan[chan].pre_trigger, pretrigger);
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Get the PreTrigger for the specified channel
 * @param[in] id caen1725 slot ID
 * @param[in] chan Channel Number
 * @param[out] pretrigger
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725GetPreTrigger(int32_t id, int32_t chan, uint32_t *pretrigger)
{
  CHECKID(id);
  CHECKCHAN(chan);

  C1725LOCK;
  *pretrigger = vmeRead32(&c1725p[id]->chan[chan].pre_trigger) & C1725_PRE_TRIGGER_MASK;
  C1725UNLOCK;

  return OK;
}


/**
 * @brief Set the TriggerThreshold for the specified channel
 * @param[in] id caen1725 slot ID
 * @param[in] chan Channel Number
 * @param[in] thres
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725SetTriggerThreshold(int32_t id, int32_t chan, uint32_t thres)
{
  CHECKID(id);
  CHECKCHAN(chan);

  if(thres > C1725_TRIGGER_THRESHOLD_MASK)
    {
      fprintf(stderr, "%s: ERROR: Invalid thres (%d)\n",
	      __func__, thres);
      return ERROR;

    }

  C1725LOCK;
  vmeWrite32(&c1725p[id]->chan[chan].trigger_threshold, thres);
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Get the TriggerThreshold for the specified channel
 * @param[in] id caen1725 slot ID
 * @param[in] chan Channel Number
 * @param[out] thres
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725GetTriggerThreshold(int32_t id, int32_t chan, uint32_t *thres)
{
  CHECKID(id);
  CHECKCHAN(chan);

  C1725LOCK;
  *thres = vmeRead32(&c1725p[id]->chan[chan].trigger_threshold) & C1725_TRIGGER_THRESHOLD_MASK;
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Set the FixedBaseline for the specified channel
 * @param[in] id caen1725 slot ID
 * @param[in] chan Channel Number
 * @param[in] baseline
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725SetFixedBaseline(int32_t id, int32_t chan, uint32_t baseline)
{
  CHECKID(id);
  CHECKCHAN(chan);

  if(baseline > C1725_FIXED_BASELINE_MASK)
    {
      fprintf(stderr, "%s: ERROR: Invalid baseline (%d)\n",
	      __func__, baseline);
      return ERROR;

    }

  C1725LOCK;
  vmeWrite32(&c1725p[id]->chan[chan].fixed_baseline, baseline);
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Get the FixedBaseline for the specified channel
 * @param[in] id caen1725 slot ID
 * @param[in] chan Channel Number
 * @param[out] baseline
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725GetFixedBaseline(int32_t id, int32_t chan, uint32_t *baseline)
{
  CHECKID(id);
  CHECKCHAN(chan);

  C1725LOCK;
  *baseline = vmeRead32(&c1725p[id]->chan[chan].fixed_baseline) & C1725_FIXED_BASELINE_MASK;
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Set the CoupleTriggerLogic for the specified channel
 * @param[in] id caen1725 slot ID
 * @param[in] chan Channel Number
 * @param[in] logic
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725SetCoupleTriggerLogic(int32_t id, int32_t chan, uint32_t logic)
{
  CHECKID(id);
  CHECKCHAN(chan);

  if(logic > C1725_COUPLE_TRIGGER_LOGIC_MASK)
    {
      fprintf(stderr, "%s: ERROR: Invalid logic (%d)\n",
	      __func__, logic);
      return ERROR;

    }

  C1725LOCK;
  vmeWrite32(&c1725p[id]->chan[chan].couple_trigger_logic, logic);
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Get the CoupleTriggerLogic for the specified channel
 * @param[in] id caen1725 slot ID
 * @param[in] chan Channel Number
 * @param[out] logic
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725GetCoupleTriggerLogic(int32_t id, int32_t chan, uint32_t *logic)
{
  CHECKID(id);
  CHECKCHAN(chan);

  C1725LOCK;
  *logic = vmeRead32(&c1725p[id]->chan[chan].couple_trigger_logic) & C1725_COUPLE_TRIGGER_LOGIC_MASK;
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Set the SamplesUnderThreshold for the specified channel
 * @param[in] id caen1725 slot ID
 * @param[in] chan Channel Number
 * @param[in] thres
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725SetSamplesUnderThreshold(int32_t id, int32_t chan, uint32_t thres)
{
  CHECKID(id);
  CHECKCHAN(chan);

  if(thres > C1725_UNDER_THRESHOLD_MASK)
    {
      fprintf(stderr, "%s: ERROR: Invalid thres (%d)\n",
	      __func__, thres);
      return ERROR;

    }

  C1725LOCK;
  vmeWrite32(&c1725p[id]->chan[chan].samples_under_threshold, thres);
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Get the SamplesUnderThreshold for the specified channel
 * @param[in] id caen1725 slot ID
 * @param[in] chan Channel Number
 * @param[out] thres
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725GetSamplesUnderThreshold(int32_t id, int32_t chan, uint32_t *thres)
{
  CHECKID(id);
  CHECKCHAN(chan);

  C1725LOCK;
  *thres = vmeRead32(&c1725p[id]->chan[chan].samples_under_threshold) & C1725_UNDER_THRESHOLD_MASK;
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Set the MaxmimumTail for the specified channel
 * @param[in] id caen1725 slot ID
 * @param[in] chan Channel Number
 * @param[in] maxtail
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725SetMaxmimumTail(int32_t id, int32_t chan, uint32_t maxtail)
{
  CHECKID(id);
  CHECKCHAN(chan);

  if(maxtail > C1725_MAX_TAIL_MASK)
    {
      fprintf(stderr, "%s: ERROR: Invalid maxtail (%d)\n",
	      __func__, maxtail);
      return ERROR;

    }

  C1725LOCK;
  vmeWrite32(&c1725p[id]->chan[chan].maximum_tail, maxtail);
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Get the MaxmimumTail for the specified channel
 * @param[in] id caen1725 slot ID
 * @param[in] chan Channel Number
 * @param[out] maxtail
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725GetMaxmimumTail(int32_t id, int32_t chan, uint32_t *maxtail)
{
  CHECKID(id);
  CHECKCHAN(chan);

  C1725LOCK;
  *maxtail = vmeRead32(&c1725p[id]->chan[chan].maximum_tail) & C1725_MAX_TAIL_MASK;
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Set the features of DPP algorithm for the specified channel
 * @param[in] id caen1725 slot ID
 * @param[in] chan Channel Number
 * @param[in] test_pulse_enable Enable internal test pulse
 * @param[in] test_pulse_rate Test Pulse rate
 * @param[in] test_pulse_polarity Pulse Polarity (0: positive, 1:negative)
 * @param[in] self_trigger_enable Enable Self-trigger (1: self trigger, 0: global trigger)
 * @return OK if successful, ERROR otherwise.
 */
int32_t
c1725SetDPPControl(int32_t id, int32_t chan,
		   uint32_t test_pulse_enable, uint32_t test_pulse_rate,
		   uint32_t test_pulse_polarity, uint32_t self_trigger_enable)
{
  uint32_t wreg = 0;
  CHECKID(id);
  CHECKCHAN(chan);

  if(test_pulse_rate > 0x3)
    {
      fprintf(stderr, "%s: ERROR: Invalid test_pulse_rate (0x%x)\n",
	      __func__, test_pulse_rate);
      return ERROR;
    }

  wreg = test_pulse_enable ? C1725_DPP_TEST_PULSE_ENABLE : 0;

  wreg |= test_pulse_rate << 9;

  wreg |= test_pulse_polarity ? C1725_DPP_TEST_PULSE_NEGATIVE : 0;

  wreg |= self_trigger_enable ? 0 : C1725_DPP_SELF_TRIGGER_DISABLE;

  C1725LOCK;
  vmeWrite32(&c1725p[id]->chan[chan].dpp_algorithm_ctrl, wreg);
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Get the features of DPP algorithm for the specified channel
 * @param[in] id caen1725 slot ID
 * @param[in] chan Channel Number
 * @param[out] test_pulse_enable Enable internal test pulse
 * @param[out] test_pulse_rate Test Pulse rate
 * @param[out] test_pulse_polarity Pulse Polarity (0: positive, 1:negative)
 * @param[out] self_trigger_enable Enable Self-trigger (1: self trigger, 0: global trigger)
 * @return OK if successful, ERROR otherwise.
 */
int32_t
c1725GetDPPControl(int32_t id, int32_t chan,
		   uint32_t *test_pulse_enable, uint32_t *test_pulse_rate,
		   uint32_t *test_pulse_polarity, uint32_t *self_trigger_enable)
{
  uint32_t rreg = 0;
  CHECKID(id);
  CHECKCHAN(chan);

  C1725LOCK;
  rreg = vmeRead32(&c1725p[id]->chan[chan].dpp_algorithm_ctrl) & C1725_DPP_CTRL_MASK;

  *test_pulse_enable = (rreg & C1725_DPP_TEST_PULSE_ENABLE) ? 1 : 0;
  *test_pulse_rate = (rreg & C1725_DPP_TEST_PULSE_RATE_MASK) >> 9;
  *test_pulse_polarity = (rreg & C1725_DPP_TEST_PULSE_NEGATIVE) ? 1 : 0;
  *self_trigger_enable = (rreg & C1725_DPP_SELF_TRIGGER_DISABLE) ? 0 : 1;

  C1725UNLOCK;

  return OK;
}

/**
 * @brief Set the Couple Over Trigger Logic for the specified channel
 * @param[in] id caen1725 slot ID
 * @param[in] chan Channel Number
 * @param[in] logic Logic value (0: AND, 1: ONLY N, 2: ONLY N+1, 3: OR)
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725SetCoupleOverTriggerLogic(int32_t id, int32_t chan, uint32_t logic)
{
  CHECKID(id);
  CHECKCHAN(chan);

  if(logic > C1725_COUPLE_TRIGGER_LOGIC_MASK )
    {
      fprintf(stderr, "%s: ERROR: Invalid logic (%d)\n",
	      __func__, logic);
      return ERROR;

    }

  C1725LOCK;
  vmeWrite32(&c1725p[id]->chan[chan].couple_trigger_logic, logic);
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Get the Couple Over Trigger Logic for the specified channel
 * @param[in] id caen1725 slot ID
 * @param[in] chan Channel Number
 * @param[out] logic Logic value (0: AND, 1: ONLY N, 2: ONLY N+1, 3: OR)
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725GetCoupleOverTriggerLogic(int32_t id, int32_t chan, uint32_t *logic)
{
  CHECKID(id);
  CHECKCHAN(chan);

  C1725LOCK;
  *logic = vmeRead32(&c1725p[id]->chan[chan].couple_trigger_logic);
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Get the Channel Status
 * @param[in] id caen1725 slot ID
 * @param[out] memory full (bit0) and empty (bit1) bits
 * @param[out] spi_busy 1 if SPI Bus is busy
 * @param[out] calibration ADC Calibration status (0: not done, 1:done)
 * @param[out] overtemp ADC Powered down on over-temperature condition
 * @return OK if successful, ERROR otherwise.
 */
int32_t
c1725GetChannelStatus(int32_t id, uint32_t chan, uint32_t *memory, uint32_t *spi_busy,
		      uint32_t *calibration, uint32_t *overtemp)
{
  uint32_t rreg = 0;
  CHECKID(id);
  CHECKCHAN(chan);

  C1725LOCK;
  rreg = vmeRead32(&c1725p[id]->chan[chan].status) & C1725_CHANNEL_STATUS_MASK;
  *memory = rreg & C1725_CHANNEL_STATUS_MEM_MASK;
  *spi_busy = (rreg & C1725_CHANNEL_STATUS_SPI_BUSY) ? 1 : 0;
  *calibration = (rreg & C1725_CHANNEL_STATUS_CALIB_DONE) ? 1 : 0;
  *overtemp = (rreg & C1725_CHANNEL_STATUS_OVERTEMP) ? 1 : 0;
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Get the ADCTemperature
 * @param[in] id caen1725 slot ID
 * @param[out] temperature
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725GetADCTemperature(int32_t id, uint32_t chan, uint32_t *temperature)
{
  CHECKID(id);
  CHECKCHAN(chan);

  C1725LOCK;
  *temperature = vmeRead32(&c1725p[id]->chan[chan].adc_temperature) & C1725_ADC_TEMP_MASK;
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Set the DCOffset for the specified channel
 * @param[in] id caen1725 slot ID
 * @param[in] chan Channel Number
 * @param[in] offset
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725SetDCOffset(int32_t id, int32_t chan, uint32_t offset)
{
  int32_t iwait=0, maxwait=1000;
  CHECKID(id);
  CHECKCHAN(chan);

  if(offset > C1725_DC_OFFSET_MASK)
    {
      fprintf(stderr, "%s: ERROR: Invalid offset (%d)\n",
	      __func__, offset);
      return ERROR;

    }

  C1725LOCK;
  /* Prescription from the manual */
  while(iwait<maxwait)
    {
      if((vmeRead32(&c1725p[id]->chan[chan].status) & C1725_CHANNEL_STATUS_SPI_BUSY)==0)
	break;
      iwait++;
    }
  C1725UNLOCK;

  if(iwait>=maxwait)
    {
      fprintf(stderr, "%s(%d, %d): ERROR: Timeout in setting the DAC\n",
	      __func__, id, chan);
      return ERROR;
    }

  C1725LOCK;
  vmeWrite32(&c1725p[id]->chan[chan].dc_offset, offset);
  C1725UNLOCK;

  return OK;
}

/**
 * @brief Get the DCOffset for the specified channel
 * @param[in] id caen1725 slot ID
 * @param[in] chan Channel Number
 * @param[out] offset
 * @return OK if successful, ERROR otherwise.
 */

int32_t
c1725GetDCOffset(int32_t id, int32_t chan, uint32_t *offset)
{
  CHECKID(id);
  CHECKCHAN(chan);

  C1725LOCK;
  *offset = vmeRead32(&c1725p[id]->chan[chan].dc_offset) & C1725_DC_OFFSET_MASK;
  C1725UNLOCK;

  return OK;
}

/**
 * @brief General Data readout routine
 * @param[in] id caen1725 slot ID
 * @param[out] data local memory address to place data
 * @param[in] nwrds Max number of words to transfer
 * @param[in] rflag Readout Flag
 *              0 - programmed I/O from the specified board
 *              1 - DMA transfer using Universe/Tempe DMA Engine
 *                    (DMA VME transfer Mode must be setup prior)
 * @return If successful, number of 4byte words added to data.  Otherwise ERROR.
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

/**
 * @brief General Data readout routine
 * @param[in] id caen1725 slot ID
 * @param[out] data local memory address to place data
 * @param[in] nwrds Max number of words to transfer
 * @param[in] rflag Readout Flag
 *              0 - programmed I/O from the specified board
 *              1 - DMA transfer using Universe/Tempe DMA Engine
 *                    (DMA VME transfer Mode must be setup prior)
 * @return If successful, number of 4byte words added to data.  Otherwise ERROR.
 */

int32_t
c1725CBLTReadBlock(volatile uint32_t *data, uint32_t nwrds, int32_t rflag)
{
  int32_t stat, retVal, xferCount;
  int32_t dummy=0;
  volatile uint32_t *laddr;
  uint32_t readout_status = 0, berr = 0;
  unsigned long vmeAdr;
  int32_t nwrds_leftover=0;
  int32_t dmas=0;

  if(c1725MCSTp == NULL)
    {
      fprintf(stderr, "%s: ERROR: MCST/CBLT Address not initialized!\n",
	     __func__);
      return ERROR;
    }

  C1725LOCK;
  /*Assume that the DMA programming is already setup. */
  /* Don't Bother checking if there is valid data - that should be done prior
     to calling the read routine */

  /* Check for 8 byte boundary for address - insert dummy word */
  if((unsigned long) (data)&0x7)
    {
      *data = LSWAP(0xcebaf111);
      dummy = 1;
      laddr = (data + 1);
      xferCount = 1;
    }
  else
    {
      dummy = 0;
      laddr = data;
      xferCount = 0;
    }


  vmeAdr = (unsigned long)(c1725MCSTp) - c1725MCSTOffset;

 DMASTART: /* Here's our Start of the CBLT, in case it needs to be repeated */

  dmas++;

  if(nwrds > (0x1000 >> 2))
    { /* Limit the DMA Transfer to less than the readout space */
      nwrds_leftover = nwrds - (0x1000 >> 2);
      nwrds = (0x1000 >> 2);
#ifdef DEBUGCBLT
      printf("%s: May need retries.  nwrds = %d  nwrds_leftover = %d\n",
	     __FUNCTION__,
	     nwrds, nwrds_leftover);
#endif
    }
#ifdef DEBUGCBLT
  printf("%s: DMAs = %d\n",__FUNCTION__,dmas);
  printf("    laddr = 0x%08x   vmeAdr = 0x%08x  nwrds<<2 = %d\n",
	 laddr, vmeAdr, nwrds<<2);
#endif

  retVal = vmeDmaSend((unsigned long)laddr, vmeAdr, (nwrds<<2));

  if(retVal != 0)
    {
      fprintf(stderr, "%s: ERROR in DMA transfer Initialization 0x%x\n",
	      __func__, retVal);
      C1725UNLOCK;
      return(retVal);
    }

  /* Wait until Done or Error */
  retVal = vmeDmaDone();

#ifdef DEBUGCBLT
  printf("%s: retVal = %d\n",
	 __func__, retVal);

  int ic;
  for(ic = 0; ic<Nc1725; ic++)
    {

      printf("%s: %d.readout_status  0x%08x\n",
	     __func__, c1725Slot(ic), vmeRead32(&c1725p[c1725Slot(ic)]->readout_status));
    }
#endif

  /* Check for BERR from last module */
  readout_status = vmeRead32(&c1725p[c1725Slot(Nc1725-1)]->readout_status);
  berr = (readout_status & C1725_READOUT_STATUS_BERR_OCCURRED) ? 1 : 0;

  if (retVal == 0)
    {
      if((xferCount - dummy) == 0)
	fprintf(stderr, "%s: WARN: DMA transfer returned zero word count 0x%x berr = %d\n",
		__func__, nwrds, berr);
      C1725UNLOCK;
      return(xferCount);
    }
  else if(retVal < 0)
    {  /* Error in DMA */
      fprintf(stderr, "%s: ERROR: vmeDmaDone returned an Error\n", __func__);
      C1725UNLOCK;
      return(retVal);
    }

  if(berr)
    {
      xferCount += (retVal >> 2);  /* Number of 4byte words transfered */
      C1725UNLOCK;
#ifdef DEBUGCBLT
      printf("%s: Done. xferCount = %d  nwrds = %d  nwrds_leftover = %d\n",
	     __FUNCTION__,
	     xferCount, nwrds, nwrds_leftover);
#endif
      return(xferCount); /* Return number of data words transfered */
    }
  else
    {
      if(nwrds_leftover > 0)
	{ /* Do it again to get the data left in the modules */
	  xferCount += nwrds;
	  laddr += nwrds;
	  nwrds = nwrds_leftover;
#ifdef DEBUGCBLT
	  printf("%s: Retry... nwrds = %d  nwrds_leftover = %d\n",
		 __FUNCTION__,
		 nwrds, nwrds_leftover);
#endif
	  goto DMASTART;
	}
      xferCount += (retVal>>2);  /* Number of Longwords transfered */
      fprintf(stderr,
	      "%s: DMA transfer terminated by unknown BUS Error (readout_status=0x%x xferCount=%d)\n",
	      __func__, readout_status, xferCount);
      C1725UNLOCK;
      return(xferCount);
    }

  C1725UNLOCK;
  return(OK);
}

uint32_t
c1725GBlockReady(uint32_t scanmask, uint32_t max_scans, uint32_t blocklevel)
{
  int32_t iscan, ic, stat=0;
  uint32_t rmask=0;

  C1725LOCK;
  for(iscan = 0; iscan < max_scans; iscan++)
    {
      for(ic = 2; ic < 21; ic++)
	{
	  if((ic < 0) || (ic >= MAX_VME_SLOTS) || (c1725p[ic] == NULL)) continue;

	  if(scanmask & (1 << ic))
	    { /* slot used */

	      if(!(rmask & (1 << ic)))
		{ /* No block ready yet. */
		  stat = (vmeRead32(&c1725p[ic]->event_stored) == blocklevel);

		  if(stat)
		    rmask |= (1 << ic);

		  if(rmask == scanmask)
		    { /* Blockready mask matches user scanmask */
		      C1725UNLOCK;
		      return(rmask);
		    }
		}
	    }
	}
    }
  C1725UNLOCK;

  return(rmask);

}
