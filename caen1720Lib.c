/* Library for the CAEN 1720 FADC 

   R. Michaels, Apr 2008

*/

#include "vxWorks.h"
#include "stdio.h"
#include "string.h"
#include "logLib.h"
#include "taskLib.h"
#include "intLib.h"
#include "iv.h"
#include "semLib.h"
#include "vxLib.h"

/* Include FADC definitions */
#include "caen1720.h"

/* Define external Functions */
IMPORT  STATUS sysBusToLocalAdrs (int, char *, char **);

/* Global variables */
int Nc1720 = 0;      /* Number of FADCs in crate */
volatile struct c1720_address *c1720p[C1720_MAX_BOARDS];  /* pointers to memory map */

static int def_acq_ctrl=0x1;       /* default acq_ctrl */
static int def_dac_val=0x1000;     /* default DAC setting for each channel */


/* Function prototypes */

int c1720DefaultSetup(int id);
int c1720_Read_Channel(unsigned int id,
                       unsigned int chan,
                       unsigned int* buffer,
                       unsigned int bufflen);

int c1720BoardReady(int id);
int c1720EventReady(int id);
int c1720GetEvSize(int id);
int c1720GetAccum(int id, int chan);
int c1720PrintStatus(int id);
int c1720PrintChanStatus(int id, int chan);
int c1720Check(int id);
int c1720Reset(int id);
int c1720Clear(int id);
int c1720SetBufOrg(int id, int code);
int c1720GetNumEv(int id);
int c1720WriteDac(int id, int chan, int dac);
int c1720BufferFree(int id, int num);
int c1720SetAcqCtrl(int id, int bits);
int c1720SetPostTrig(int id, int val);
int c1720StartRun(int id);
int c1720StopRun(int id);
int c1720PrintBuffer();
int c1720TestPrintBuffer();
int c1720SoftTrigger(int id);

int c1720Test1();
int c1720Test1a();
int c1720Test1b();
int c1720Test2();
int c1720Test2a();
int c1720Test2b();
int c1720Test3();
int c1720Test4();


/*  c1720Init  -- Initializes the CAEN 1720 library 

    Returns: OK or ERROR 

*/

STATUS c1720Init(UINT32 addr, UINT32 addr_inc, int nadc) {

  int i, res, rdata, errFlag=0;
  int boardID=0;
  unsigned int laddr;

  if (addr < 0xffffff) {  /* A24 addressing */

      res = sysBusToLocalAdrs (0x39, (char *) addr, (char **) &laddr);

  } else {

      res = sysBusToLocalAdrs (0x09, (char *) addr, (char **) &laddr);

  }

  printf("c1720: sysBusToLocalAdrs result 0x%x  0x%x  %d \n",addr,laddr,res);
  if (res != 0) {
     printf ("c1720Init: ERROR in sysBusToLocalAdrs (0x0d, 0x%x, &laddr) \n", 
        addr);
     return (ERROR);
  }

  Nc1720 = 0;
  for (i = 0; i < nadc; i++) 
    {
      c1720p[i] = (struct c1720_address *) (laddr + i * addr_inc);
      printf("Long address adc %d   laddr = 0x%x  addr_inc = 0x%x   ptr = 0x%x \n",i,laddr,addr_inc,c1720p[i]);
      /* Check if Board exists at that address */
      res = vxMemProbe ((char *) &(c1720p[i]->board_id) , VX_READ, 4,
			(char *) &rdata);
      if (res < 0) {
	  printf ("C1720Init: ERROR: No addressable board at addr = 0x%x\n",
		  (UINT32) c1720p[i]);
	  c1720p[i] = NULL;
	  errFlag = 1;
	  break;
	}
      Nc1720++;
      printf ("c1720: Initialized ADC ID %d at address 0x%08x \n", i,
	      (UINT32) c1720p[i]);
    }
    if (errFlag > 0)  {
      printf ("c1720Init: ERROR: Unable to initialize all ADC Modules\n");
      if (Nc1720 > 0) 
	printf ("c1720Init: %d ADC (s) successfully initialized\n",
		Nc1720);
      return (ERROR);
    } else {
      return (OK);
    }
}


int c1720Check(int id) {

  if (!Nc1720 || id >= Nc1720) {
      printf("C1720:Check:ERROR: did not init board %d \n",id);
      return ERROR;
  } else { 
    if( c1720p[id]==NULL) {
      printf("C1720:Check ERROR: board %d  points to NULL \n",id);
      return ERROR;
    }
  }
  return OK;
}

int c1720PrintChanStatus(int id, int chan) {

  if (c1720Check(id)==ERROR) return ERROR;
  if (chan < 0 || chan > 8) return ERROR;

  printf("Channel %d   status (0x1%d88) = 0x%x \n",chan,chan,c1720p[id]->chan[chan].status);
  printf("      firmware (0x1%d8c) = 0x%x    buff. occ. (0x1%d94) = %d \n",
     chan, c1720p[id]->chan[chan].fpga_firmware,chan, c1720p[id]->chan[chan].buffer_occupancy);
  printf("     dac (0x1%d98) = 0x%x    threshold (0x1%d84) = 0x%x \n",
     chan, c1720p[id]->chan[chan].dac,chan, c1720p[id]->chan[chan].thresh);
  
}


int c1720PrintStatus(int id) {

  int chan_print = 1;
  int ichan;

  if (c1720Check(id)==ERROR) return ERROR;

  printf("\nCAEN 1720 board %d status \n",id);
  printf("Firmware (8124)   = 0x%x \n",c1720p[id]->firmware); 
  printf("Board info (8140) = 0x%x \n",c1720p[id]->board_info); 
  printf("Chan config (8000) = 0x%x \n",c1720p[id]->chan_config);
  printf("Buffer org (800c) = 0x%x \n",c1720p[id]->buffer_org);
  printf("Acq control (8100) = 0x%x \n",c1720p[id]->acq_ctrl);
  printf("Acq status (8104)= 0x%x \n",c1720p[id]->acq_status);
  printf("Relocation address (EF10) = 0x%x \n",c1720p[id]->reloc_addr);
  printf("VME Status (EF04) = 0x%x \n",c1720p[id]->vme_status);
  printf("Board id = (EF08) = 0x%x \n",c1720p[id]->board_id);
  printf("Interrupt id (EF14) = 0x%x\n",c1720p[id]->interrupt_id);

  if (chan_print) {
    for (ichan = 0; ichan < 8; ichan++) {
       c1720PrintChanStatus(id,ichan);
    }
  }

  return OK;
 
}

int c1720Reset(int id) {

 /*  To reset the board -- clear output buffer, event counter,
     and performs a FPGAs global reset to restore FPGAs to 
     their default config.  Also initializes counters to 
     their initial state and clears all error conditions. */


  if (c1720Check(id)==ERROR) return ERROR;

  c1720p[id]->sw_reset = 1;
  c1720p[id]->vme_ctrl = 0x10; 
  c1720p[id]->enable_mask = 0xff;

  return OK;

}

int c1720Clear(int id) {

  /*  To clear the output buffer */

  if (c1720Check(id)==ERROR) return ERROR;

  c1720p[id]->sw_clear = 1;
  c1720SetAcqCtrl(id, 0);

  return OK;

}

int c1720SoftTrigger(int id) {

  if (c1720Check(id)==ERROR) return ERROR;

  c1720p[id]->sw_trigger = 1;

  return OK;

}


int c1720GetEvSize(int id) {
  /* not sure this is meaningful (some docs says there's no event_size word) */

  if (c1720Check(id)==ERROR) return 0;

  return c1720p[id]->event_size; 
  

}


int c1720GetNumEv(int id) {

/*  To obtain the number of events stored in buffer */

  if (c1720Check(id)==ERROR) return 0;

  return c1720p[id]->event_stored; 

}

int c1720WriteDac(int id, int chan, int dac) {
/* Write DAC (offset value) to channel #chan in board #id */

  if (c1720Check(id)==ERROR) return ERROR;
  if (chan < 0 || chan > 8) return ERROR;

  printf("c1720: Writing DAC for id=%d  chan=%d   value=%d\n",id,chan,dac);

  c1720p[id]->chan[chan].dac = dac; 

  return 0;

}

int c1720BufferFree(int id, int num) {

  if (c1720Check(id)==ERROR) return ERROR;

  printf("c1720: Into Buffer Free, num = %d \n",num);

  c1720p[id]->buffer_free = num;

  return 0;

}


int c1720SetAcqCtrl(int id, int bits) {

  int acq;

  if (c1720Check(id)==ERROR) return ERROR;

  acq = c1720p[id]->acq_ctrl;
  c1720p[id]->acq_ctrl = acq | bits;
 
  return 0;
}

int c1720SetPostTrig(int id, int val) {

  if (c1720Check(id)==ERROR) return ERROR;

  c1720p[id]->post_trigset = val;

  return 0;

}

int c1720BoardReady(int id) {

  return ((c1720p[id]->acq_status & 0x100)==0x100);
 
  return 0;
}

int c1720EventReady(int id) {

  int i1,i2;

  i1 = 0; 
  i2 = 0; 
  if ((c1720p[id]->acq_status & 0x8)==0x8) i1=1;
  if ((c1720p[id]->vme_status & 0x1)==0x1) i2=1;
  if (i1 && i2) return 1;
 
  return 0;
}

int c1720SetBufOrg(int id, int code) {

  if (c1720Check(id)==ERROR) return ERROR;

  c1720p[id]->buffer_org = code;
 
  return 0;
}

  
int c1720DefaultSetup(int id) {

  int loop, maxloop, chan;
  maxloop = 10000;

  if (c1720Check(id)==ERROR) return ERROR; 

  c1720Reset(id);

  loop=0;
  while (loop++ < maxloop) {
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

  c1720p[id]->chan_config = 0x10;

  for (chan=0; chan<8; chan++) {
    c1720WriteDac(id, chan, def_dac_val);    
  }

  return 0;

} 

int c1720StartRun(int id) {

  int acq;

  printf("\nc1720: Starting a run \n");

  if (c1720Check(id)==ERROR) return ERROR; 

  acq = c1720p[id]->acq_ctrl;
  c1720p[id]->acq_ctrl = acq | 0x4;

  return 0;
} 

int c1720StopRun(int id) {

  int acq;

  printf("\nc1720: Stopping a run \n");

  if (c1720Check(id)==ERROR) return ERROR; 

  acq = c1720p[id]->acq_ctrl;
  acq = acq & 0xb;  /* kill the 0x4 bit */
  c1720p[id]->acq_ctrl = acq ;

  return 0;
} 


int c1720Test1() {
  
  int myid=0;

  c1720Test1a(myid);

  c1720Test1b(myid);

  return 0;
}

int c1720Test1a(int myid) {

  if (c1720DefaultSetup(myid)==ERROR) {
    printf("C1720: ERROR: Cannot setup board.  Give up !\n");
    return ERROR;
  }

  taskDelay(1*60);

  c1720PrintStatus(myid);

}

int c1720Test1b(int myid) {

/* After 1a, and prior to this, plug in the S-IN (False->True) */

  int loop, maxloop, nev;

  maxloop = 500000;

  c1720SoftTrigger(myid); 

  taskDelay(1*60);  /* wait N sec */
  
  loop=0;
  while (loop++ < maxloop) {
    if (c1720EventReady(myid)) break;
  }

  nev = c1720GetNumEv(myid);

  if (loop < maxloop) {
     printf("\nEvent ready \n");
  } else {
     printf("\nEvent NOT ready !\n");
  }  

  printf("\n ----------------------------------------- \n Num of events  = %d     Size = %d  loop = %d \n",nev,c1720GetEvSize(myid),loop);

  c1720PrintStatus(myid);

  if (nev > 0 ) c1720PrintBuffer(myid);

  return 0;

}


int c1720Test2() {

  int myid=0;

  c1720Test2a(myid);

  /* may plug in trigger now */

  c1720Test2b(myid);

  if (c1720EventReady(myid)==1) {
    printf("\n -- Event is ready -- \n");
  } else {
    printf("\n -- Event NOT ready -- \n");
  }

  return 0;
}

int c1720Test2a(int myid) {

  // Preliminaries of Test 2

  // Use register-controlled mode.
  
  int loop, maxloop, chan;
  int my_acq_ctrl = 0;

  maxloop = 50000;

  c1720Reset(myid);

  loop=0;
  while (loop++ < maxloop) {
    if (c1720BoardReady(myid)) break;
  }

  c1720Clear(myid); 

  for (chan=0; chan<8; chan++) {
    c1720WriteDac(myid, chan, def_dac_val);    
  }
 
  c1720SetBufOrg(myid, 4);
  c1720SetPostTrig(myid, 44);

  c1720SetAcqCtrl(myid, my_acq_ctrl);


  c1720p[myid]->trigmask_enable = 0xc0000000; 
  c1720p[myid]->chan_config = 0x10;
  c1720p[myid]->enable_mask = 0xff; 

  taskDelay(2*60);

  printf("\n ----- STATUS BEFORE RUN (2a)--------- \n");
  c1720PrintStatus(myid);

  return 0;
}


int c1720Test2b(int myid) {

  /* last part of Test2.  Do after plugging in trigger */

  int loop;

  c1720StartRun(myid);

  loop=0;
  while (loop++ < 500000) {
    if (c1720EventReady(myid)) {  /* should not be ready until 'StopRun' */
       printf("Event Ready\n");
       break;
    }
  }
  printf("Chk Event ready loop1 = %d \n",loop);

  printf("\n ----- STATUS AFTER RUN (2b) --------- \n");
  c1720PrintStatus(myid);

  printf("Num of events  = %d     Size = %d \n",c1720GetNumEv(myid),c1720GetEvSize(myid));

  if (c1720GetNumEv(myid)>0) c1720PrintBuffer(myid);


  return 0;

}

int c1720Test3() {

  // Just checking registers
  
  int myid=0;
  int loop, maxloop;
  int my_acq_ctrl = 0x2;

  maxloop = 50000;

  c1720Reset(myid);

  loop=0;
  while (loop++ < maxloop) {
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

int c1720TestPrintBuffer() {
/* test code (temp) */

  int i;
  unsigned long laddr, data;
  volatile unsigned long *bdata;
  sysBusToLocalAdrs(0x39,0x09000000,&laddr);
  bdata = (volatile unsigned long *)laddr;

  printf("\nTest Print\n");
  for (i=0; i<10; i++) {
     data = *bdata;
     printf("data[%d] = %d = 0x%x \n",i,data,data);
  } 

  return OK; 

}

int c1720PrintBuffer(int id) {

  int ibuf,i,j;
  int d1,d2,d3,d4,d5,d6,d7,d8; 
  unsigned long *adrvme;
  unsigned long data;

  if (c1720Check(id)==ERROR) return ERROR; 

  for (ibuf=0; ibuf<5; ibuf++) {

    printf("c1720: Print Buf %d \n",ibuf);

    for (i=0; i<10; i++) {

      d1 = c1720p[id]->readout_buffer[ibuf];
      printf("    Data[%d] = %d = 0x%x\n",i,d1,d1);

    }
  }

  return 0;
}


int c1720Test4(int nloop) {

  int i,j;

  

  for (i=0; i<nloop; i++) {

    printf("\n\ndoing loop %d \n",i);
    c1720Test1();
    for (j=0; j<5000; j++) {
      c1720BoardReady(0);
    }
    taskDelay(2*60);

  }
  return 0;

}
