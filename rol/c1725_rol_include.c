#pragma once
/*************************************************************************
 *
 *  c1725_rol_include.c -
 *
 *   Library of routines readout and buffering of events from CAEN1725
 *
 */

#include "caen1725Lib.h"
#include "caen1725Config.h"

/* C1725 Library Variables */
#define NC1725     1
/* Address of first fADC250 */
#define C1725_ADDR (3<<19)
/* Increment address to find next fADC250 */
#define C1725_INCR (1<<19)
#define C1725_BANK 1725

#define DOALL(x) {				\
    int32_t _ic=0;				\
    for(_ic = 0; _ic < c1725N(); _ic++)		\
      {						\
	x;					\
      }						\
  }

/* for the calculation of maximum data words in the block transfer */
unsigned int MAXC1725WORDS=0;

void
c1725_Download(char* configFilename)
{
  int ifa, stat;

  /*****************
   *   C1725 SETUP
   *****************/

  caen1725ConfigInitGlobals();

  // INIT
  c1725Init(C1725_ADDR, C1725_INCR, NC1725);

  /* configure all modules based on config file */
  caen1725Config(configFilename);

  c1725SetMulticast(0x09000000);

  c1725GStatus(1);

  printf("%s: done\n", __func__);

}

void
c1725_Prestart()
{

  /* Program/Init VME Modules Here */
  /* DOALL(...); */

  c1725GStatus(1);

  printf("%s: done\n", __func__);

}

void
c1725_Go()
{

  /* Set the current block level */
  DOALL(c1725SetMaxEventsPerBLT(c1725Slot(_ic), blockLevel));


  /* Get the C1725 mode and window size to determine max data size */
  // ...
  uint32_t ptw = 0;

  /* Set Max words from fadc (proc mode == 1 produces the most)
     nfadc * ( Block Header + Trailer + 2  # 2 possible filler words
               blockLevel * ( Event Header + Header2 + Timestamp1 + Timestamp2 +
	                      nchan * (Channel Header + (WindowSize / 2) )
             ) +
     scaler readout # 16 channels + header/trailer
   */
  MAXC1725WORDS = c1725N() * (4 + blockLevel * (4 + 16 * (1 + (ptw / 2))) + 18);

  /*  Enable C1725 */
  uint32_t lvds_busy_enable = 0, lvds_veto_enable = 0, lvds_runin_enable = 0,
    mode = 0,        // 0: SW controlled
    clocksource = 0, // 0: internal
    arm = 1;         // 0: Stop, 1: Start

  DOALL(c1725SetAcquisitionControl(c1725Slot(_ic), mode, arm, clocksource,
				   lvds_busy_enable, lvds_veto_enable,
				   lvds_runin_enable));

  /* Interrupts/Polling enabled after conclusion of rocGo() */
}

void
c1725_End()
{

  /* C1725 Disable */
  uint32_t lvds_busy_enable = 0, lvds_veto_enable = 0, lvds_runin_enable = 0,
    mode = 0,        // 0: SW controlled
    clocksource = 0, // 0: internal
    arm = 0;         // 0: Stop, 1: Start

  DOALL(c1725SetAcquisitionControl(c1725Slot(_ic), mode, arm, clocksource,
				   lvds_busy_enable, lvds_veto_enable,
				   lvds_runin_enable));

  /* C1725 Event status - Is all data read out */
  c1725GStatus(1);

  printf("%s: done\n", __func__);

}

void
c1725_Trigger(int arg)
{
  int32_t stat = 0, nwords = 0, roCount = 0;
  uint32_t datascan = 0, scanmask = 0;

  roCount = tiGetIntCount();

  /* Setup Address and data modes for DMA transfers
   *
   *  vmeDmaConfig(addrType, dataType, sstMode);
   *
   *  addrType = 0 (A16)    1 (A24)    2 (A32)
   *  dataType = 0 (D16)    1 (D32)    2 (BLK32) 3 (MBLK) 4 (2eVME) 5 (2eSST)
   *  sstMode  = 0 (SST160) 1 (SST267) 2 (SST320)
   */
  vmeDmaConfig(2,5,2);

  /* C1725 Readout */
  BANKOPEN(C1725_BANK, BT_UI4, blockLevel);

  /* Mask of initialized modules */
  scanmask = c1725SlotMask();
  /* Check scanmask for block ready up to 100 times */
  datascan = c1725GBlockReady(scanmask, 100, blockLevel);
  stat = (datascan == scanmask);

  if(stat)
    {
      if(c1725N() == 1)
	nwords = c1725ReadEvent(c1725Slot(0), dma_dabufp, MAXC1725WORDS, 0);
      else
	nwords = c1725CBLTReadBlock(dma_dabufp, MAXC1725WORDS, 0);


      if(nwords <= 0)
	{
	  printf("ERROR: C1725 Data transfer (event = %d), nwords = 0x%x\n",
		 roCount, nwords);

	}
      else
	{
	  dma_dabufp += nwords;
	}
    }
  else
    {
      printf("ERROR: Event %d: Datascan != Scanmask  (0x%08x != 0x%08x)\n",
	     roCount, datascan, scanmask);
    }
  BANKCLOSE;


  /* Check for SYNC Event */
  if(tiGetSyncEventFlag() == 1)
    {
      int32_t ic, id;
      for(ic = 0; ic < c1725N(); ic++)
	{
	  id = c1725Slot(ic);

	  uint32_t event_ready = 0, berr = 0, vme_fifo_empty = 0, evstored = 0;
	  c1725GetReadoutStatus(id, &event_ready, &berr, &vme_fifo_empty);

	  if((vme_fifo_empty != 0) || (event_ready == 1) || (evstored != 0))
	    {
	      printf("%s: ERROR: C1725 Data available after readout in SYNC event.\n",
		     __func__);
	      printf("%s: event_ready = %d  vme_fifo_empty = %d  evstored = %d\n",
		     __func__, event_ready, vme_fifo_empty, evstored);

	      c1725Clear(id);
	    }
	}
    }

}

void
c1725_Cleanup()
{

  printf("%s: Reset C1725s\n",__func__);
  DOALL(c1725Reset(c1725Slot(_ic)));

}

/*
  Local Variables:
  compile-command: "make -k ti_c1725_list.so "
  End:
 */
