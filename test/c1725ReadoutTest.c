/*
 * File:
 *    c1725ReadoutTest.c
 *
 * Description:
 *    Test the caen 1725 library readout
 *
 *
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
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

  c1725GStatus(1);
  c1725ChannelsStatus(3,1);

  // START ACQ
  uint32_t lvds_busy_enable = 0, lvds_veto_enable = 0, lvds_runin_enable = 0,
    mode = 0,        // 0: SW controlled
    clocksource = 0, // 0: internal
    arm = 1;         // 0: Stop, 1: Start

  DOALL(c1725SetAcquisitionControl(c1725Slot(_ic), mode, arm, clocksource,
				   lvds_busy_enable, lvds_veto_enable,
				   lvds_runin_enable));
  sleep(1);
  // Soft trigger
  DOALL(c1725SoftTrigger(c1725Slot(_ic)));
  DOALL(c1725SoftTrigger(c1725Slot(_ic)));
  DOALL(c1725SoftTrigger(c1725Slot(_ic)));
  DOALL(c1725SoftTrigger(c1725Slot(_ic)));

  // Check for data
  int32_t ic;
  for(ic = 0; ic < c1725N(); ic++)
    {
      uint32_t event_ready = 0, berr = 0, vme_fifo_empty = 0;
      c1725GetReadoutStatus(c1725Slot(ic), &event_ready, &berr, &vme_fifo_empty);
      printf("event_ready = %d    berr = %d    vme_fifo_empty = %d\n",
	     event_ready, berr, vme_fifo_empty);

    }

  c1725GStatus(1);
  // Readout

  // STOP
  arm = 0; // 0: Stop, 1: Start
  DOALL(c1725SetAcquisitionControl(c1725Slot(_ic),
				   mode, arm, clocksource,
				   lvds_busy_enable, lvds_veto_enable,
				   lvds_runin_enable));


 CLOSE:


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
