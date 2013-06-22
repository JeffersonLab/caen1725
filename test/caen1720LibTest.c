/*
 * File:
 *    caen1720LibTest.c
 *
 * Description:
 *    Test the caen 1720 library
 *
 *
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "caen1720Lib.h"
#include "remexLib.h"

void
isr()
{
  int iwait=0, maxwait=1000;
  int nwrds=0;
  volatile unsigned int data[4200];
  while(iwait<maxwait)
    {
      if(c1720EventReady(0))
	{
	  nwrds = c1720ReadEvent(0,(volatile unsigned int *)&data,4200,0);
	  if(nwrds)
	    {
	      printf("nwrds = %d\n",nwrds);
	      int i=0;
	      for(i=0; i<nwrds; i++)
		{
		  int word = data[i];
		  printf("  0x%08x",word);
		  if(((i+1)%5)==0) printf("\n");
		}
	      printf("\n");
	    }
	  else
	    {
	      printf("dah... nwrds = %d\n",nwrds);
	    }
	  break;
	}
      iwait++;
    }
  if(iwait>=maxwait)
    printf("No trigger\n");

}

int 
main(int argc, char *argv[]) 
{

  extern int Nc1720;

  printf("\nJLAB CAEN 1720 Library Tests\n");
  printf("----------------------------\n");

  vmeOpenDefaultWindows();

  remexSetCmsgServer("dafarm28");
  remexInit(NULL,1);
  remexSetRedirect(1);

  if(c1720Init(0xa00000,0,1)!=OK)
    if(Nc1720==0)
      goto CLOSE;

/*   c1720Reset(0); */
  c1720Clear(0);

  c1720PrintStatus(0);

  int ichan=0, DAC=0xfff>>2, THRES=0xb00;
  for(ichan=0; ichan<8; ichan++)
    {
      c1720SetChannelDAC(0, ichan, DAC);
      c1720SetChannelThreshold(0, ichan, THRES);
      c1720SetChannelTimeOverUnder(0,ichan,10);
    }

  c1720SetBufferSize(0,64);
  c1720SetPostTrig(0,45);
/*   c1720SetBufOrg(0,0x0a); */
  c1720SetEnableChannelMask(0,1<<2);
  c1720DisableTriggerSource(0,3,0xff);
  c1720EnableTriggerSource(0,2,1<<2,0);
  c1720SetTriggerOnUnderThreshold(0,1);

  int vector=0xe1, level=5;
  c1720SetupInterrupt(0,level,vector);
  if(vmeIntConnect(vector,level,isr,1) != OK) 
    {
      printf("ERROR in intConnect()\n");
    }
  

  printf("<Enter> to start run\n");
  getchar();

  c1720EnableInterrupts(0);

  printf("Board Ready = %d\n",c1720BoardReady(0));
  printf("Events Ready = %d\n",c1720GetNumEv(0));
  c1720StartRun(0);
  c1720PrintStatus(0);
/*   c1720SoftTrigger(0); */

  printf("<Enter> to get trigger\n");
  getchar();


 CLOSE:
  c1720StopRun(0);
  c1720DisableInterrupts(0);
  c1720SetEnableChannelMask(0,0);
  c1720DisableTriggerSource(0,3,0xff);

  c1720PrintStatus(0);
  if(vmeIntDisconnect(level) != OK)
    {
      printf("ERROR disconnecting Interrupt\n");
    }

  remexClose();
  vmeCloseDefaultWindows();

  exit(0);
}

