/*
 * File:
 *    c1725ReadoutTest.c
 *
 * Description:
 *    Test the caen 1725 library readout
 *
 *
 */


#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <byteswap.h>
#include "jvme.h"
#include "caen1725Lib.h"
#include "caen1725Config.h"

#define DOALL(x) {				\
    int32_t _ic=0;				\
    for(_ic = 0; _ic < c1725N(); _ic++)		\
      {						\
	x;					\
      }						\
  }


int
main(int argc, char *argv[])
{
  int32_t stat, ninit = 1;
  uint32_t address=0;

  address = (2 << 19);
  ninit = 20;

  printf("\n %s: config = %s \n", argv[0], (argc == 2) ? argv[1] : "none");
  printf("----------------------------\n");

  stat = vmeOpenDefaultWindows();
  if(stat != OK)
    goto CLOSE;

  vmeCheckMutexHealth(1);
  vmeBusLock();

  caen1725ConfigInitGlobals();

  // INIT
  c1725Init(address, (1 << 19), ninit);
  /* Reset before we close, for testing... */
  DOALL(c1725Reset(c1725Slot(_ic)));
  sleep(1);
  c1725Init(address, (1 << 19), ninit);

  // CONFIG
  if(argc == 2)
    caen1725Config(argv[1]);

  c1725SetMulticast(0x09000000);


  c1725GStatus(1);
  c1725ChannelsStatus(3,1);

  printf("<enter> to start acq + triggers \n");
  getchar();
  // START ACQ
  uint32_t lvds_busy_enable = 0, lvds_veto_enable = 0, lvds_runin_enable = 0,
    mode = 0,        // 0: SW controlled
    clocksource = 0, // 0: internal
    arm = 1;         // 0: Stop, 1: Start

  DOALL(c1725SetAcquisitionControl(c1725Slot(_ic), mode, arm, clocksource,
				   lvds_busy_enable, lvds_veto_enable,
				   lvds_runin_enable));

  // Soft trigger
  /* DOALL(c1725SoftTrigger(c1725Slot(_ic))); */

  // Check for data
  uint32_t datascan = 0, scanmask = c1725SlotMask();
  datascan = c1725GBlockReady(scanmask, 100, 1);

  if(datascan == scanmask)
    {
      printf("Data available (scanmask = 0x%x)\n", scanmask);
    }
  else
    {
      printf("Data NOT available (scanmask = 0x%x, datascan = 0x%x)\n", scanmask,
	     datascan);
    }


  int32_t ic, id;
#ifdef OLD
  for(ic = 0; ic < c1725N(); ic++)
    {
      uint32_t event_ready = 0, berr = 0, vme_fifo_empty = 0, evstored = 0;
      id = c1725Slot(ic);
      c1725GetReadoutStatus(id, &event_ready, &berr, &vme_fifo_empty);
      c1725GetEvStored(id, &evstored);
      printf("%2d:  event_ready = %d    berr = %d    vme_fifo_empty = %d   evstored = %d\n",
	     id, event_ready, berr, vme_fifo_empty, evstored);
    }
#endif

  // Readout
  DMA_MEM_ID vmeIN,vmeOUT;
  extern DMANODE *the_event;
  extern unsigned int *dma_dabufp;

  dmaPFreeAll();
  vmeIN  = dmaPCreate("vmeIN",10240 * 4,1,0);
  vmeOUT = dmaPCreate("vmeOUT",0,0,0);

  dmaPStatsAll();

  dmaPReInitAll();
  GETEVENT(vmeIN,0);
  int32_t nwrds = 0;
  //#define SCT
#ifdef SCT
  for(ic = 0; ic < c1725N(); ic++)
    {
      id = c1725Slot(ic);
      nwrds = c1725ReadEvent(id, dma_dabufp, 10240, 0);
      printf(" nwrds = %d\n", nwrds);

      if(nwrds > 0)
	{
	  dma_dabufp += nwrds;
	}
    }
#else
  vmeDmaConfig(2, 3, 0);
  nwrds = c1725CBLTReadBlock(dma_dabufp, 1024, 0);
  printf(" nwrds = %d\n", nwrds);
  if(nwrds > 0)
    {
      dma_dabufp += nwrds;
    }
#endif

  PUTEVENT(vmeOUT);
  DMANODE *outEvent = dmaPGetItem(vmeOUT);

  int32_t iw = 0;

  printf(" length = %ld\n", outEvent->length);
  for(iw = 0; iw < outEvent->length; iw++)
    {
      if((iw % 8) == 0) printf("\n");
      printf("0x%08x  ", bswap_32(outEvent->data[iw]));
    }
  printf("\n");



  printf("<enter> to stop acq + triggers \n");
  getchar();
  // STOP
  arm = 0; // 0: Stop, 1: Start
  DOALL(c1725SetAcquisitionControl(c1725Slot(_ic),
				   mode, arm, clocksource,
				   lvds_busy_enable, lvds_veto_enable,
				   lvds_runin_enable));


  c1725GStatus(1);

 CLOSE:

  dmaPFreeAll();

  caen1725ConfigFree();

  vmeBusUnlock();

  vmeClearException(1);

  stat = vmeCloseDefaultWindows();
  if (stat != OK)
    {
      printf("vmeCloseDefaultWindows failed: code 0x%08x\n",stat);
      return -1;
    }

  exit(0);
}
/*
  Local Variables:
  compile-command: "make -k c1725ReadoutTest "
  End:
*/
