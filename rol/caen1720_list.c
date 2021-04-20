/*************************************************************************
 *
 *  caen1720_list.c - Library of routines for readout of
 *                    events using user defined routine in CODA 3.0
 *
 */

/* Event Buffer definitions */
#define MAX_EVENT_POOL     100
#define MAX_EVENT_LENGTH   1152*32      /* Size in Bytes */

#include "GEN_source.h" /* source required for CODA */
#include <unistd.h>

#include "jvme.h"
extern DMANODE *the_event; /* node pointer for event buffer obtained from GETEVENT,
			      declared in dmaPList */
extern unsigned int *dma_dabufp; /* event buffer pointer obtained from GETEVENT,
				    declared in dmaPList */
/* Input and Output Partitions for VME Readout */
DMA_MEM_ID vmeIN;

#include "caen1720Lib.h"

int blklevel = 1;

/*
  Type 0xff10 is RAW trigger No timestamps
  Type 0xff11 is RAW trigger with timestamps (64 bits)
*/
int trigBankType = 0xff10;

/* Global Flag for debug printing */
int usrDebugFlag=0;

/****************************************
 *  DOWNLOAD
 ****************************************/
void
rocDownload()
{


  /* Initialize memory partition library */
  dmaPartInit();

  /* Allocate memory for DMA */
  dmaPFreeAll();
  vmeIN  = dmaPCreate("vmeIN",MAX_EVENT_LENGTH,1,0);

  if(vmeIN == 0)
    daLogMsg("ERROR", "Unable to allocate memory for event buffers");

  /* Reinitialize the Buffer memory */
  dmaPReInitAll();
  dmaPStatsAll();

  /* Setup Address and data modes for DMA transfers
   *
   *  vmeDmaConfig(addrType, dataType, sstMode);
   *
   *  addrType = 0 (A16)    1 (A24)    2 (A32)
   *  dataType = 0 (D16)    1 (D32)    2 (BLK32) 3 (MBLK) 4 (2eVME) 5 (2eSST)
   *  sstMode  = 0 (SST160) 1 (SST267) 2 (SST320)
   */
  vmeDmaConfig(2,3,0);

  /* CAEN1720 library initialization */
  c1720Init(0xa00000,0,1);

  c1720Clear(0);

  c1720PrintStatus(0);


  printf("rocDownload: User Download Executed\n");

}

/****************************************
 *  PRESTART
 ****************************************/
void
rocPrestart()
{
  usrDebugFlag=0;

  /* Configure caen 1720s */
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

  c1720PrintStatus(0);

  printf("rocPrestart: User Prestart Executed\n");

}

/****************************************
 *  GO
 ****************************************/
void
rocGo()
{
  c1720PrintStatus(0);
  c1720StartRun(0);

}

/****************************************
 *  END
 ****************************************/
void
rocEnd()
{

  c1720StopRun(0);
  c1720PrintStatus(0);

  dmaPStatsAll();

  printf("rocEnd: Ended after %d events\n",*(rol->nevents));

}

/****************************************
 *  EVENT TYPE
 ****************************************/
int
rocType()
{
  static int count = 0;
  int rval = 0;

  rval = (++count % 3) + 1;

  return rval;
}

/****************************************
 *  POLLING ROUTINE
 ****************************************/
int
rocPoll()
{
  static int count = 0;
  int rval = 0;

  rval = c1720EventReady(0);
  if(rval > 0)
    {
      count++;
      return 1;
    }
  else
    return 0;

  return 0;
}


/****************************************
 *  TRIGGER
 ****************************************/
void
rocTrigger(int evno, int evtype)
{
  int ii;

  printf("%s: evno = %d, evtype = %d\n",
	 __func__, evno, evtype);

  CEOPEN(ROCID, BT_BANK, blklevel);


  /* Create Dummy trigger Bank */
  CBOPEN(trigBankType, BT_SEG, blklevel);
  for(ii = 0; ii < blklevel; ii++)
    {
      if(trigBankType == 0xff11)
	{
	  *rol->dabufp++ = (evtype << 24) | (0x01 << 16) | (3);
	}
      else
	{
	  *rol->dabufp++ = (evtype << 24) | (0x01 << 16) | (1);
	}
      *rol->dabufp++ = (blklevel * (evno - 1) + (ii + 1));
      if(trigBankType == 0xff11)
	{
	  *rol->dabufp++ = 0x12345678;
	  *rol->dabufp++ = 0;
	}
    }
  CBCLOSE;

  the_event = dmaPGetItem(vmeIN);
  if(the_event == (DMANODE *) 0)
    {
      daLogMsg("ERROR","DMA BUFFER ERROR: no pool buffer available for part %s\n",
	       vmeIN->name);
    }
  else
    {
      dma_dabufp = (unsigned int *) &(the_event->data[0]);
    }

  /* Readout event from c1720 */
  int nwrds = c1720ReadEvent(0, dma_dabufp, 4200, 0);
  if(nwrds)
    {
      dma_dabufp += nwrds;
    }
  else
    {
      printf("%s: ERROR: c1720ReadEvent returned %d\n",
	     __func__, nwrds);
    }

  /* Copy it into the event buffer */
  long length = (((long)(dma_dabufp) - (long)(&the_event->data[0]))>>2);

  /* Add this data to Bank: Number = 1720, Type = 4byte unsigned integers */
  CBOPEN(1720, BT_UI4, blklevel);
  for(ii = 0; ii < length; ii++)
    *rol->dabufp++ = the_event->data[ii];
  CBCLOSE;

  dmaPFreeItem(the_event);

  CECLOSE;

}

void
rocReset()
{
  /* Free all allocated memory for DMA */
  dmaPFreeAll();
}

void
rocCleanup()
{
  /* Free all allocated memory for DMA */
  dmaPFreeAll();

  printf("%s: Reset all Modules\n",__FUNCTION__);

}

int
tsLive(int sflag)
{
  return 100;
}
