#ifndef __V1720LIBH__
#define __V1720LIBH__
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
 *     Header for Library for the CAEN 1720 FADC 
 *
 * SVN: $Rev$
 *
 *----------------------------------------------------------------------------*/

#define C1720_MAX_BOARDS         8
#define C1720_MAX_ADC_CHANNELS   8


/* Infomation related to each channel (in address map below) */
struct c1720_chan 
{  /* 64 long words, 256 bytes */
  /* 0x1n80 */ volatile unsigned int thresh;
  /* 0x1n84 */ volatile unsigned int time_overunder;
  /* 0x1n88 */ volatile unsigned int status;
  /* 0x1n8C */ volatile unsigned int fpga_firmware;
  /* 0x1n90 */          unsigned int dummy1;
  /* 0x1n94 */ volatile unsigned int buffer_occupancy;
  /* 0x1n98 */ volatile unsigned int dac;
  /* 0x1n9C */ volatile unsigned int adc_config;
  /* 0x1nA0 */          unsigned int dummy2[(0x1180-0x10A0)/4];
};


/* Configuration ROM  (in address map below) */
struct c1720_romAddr 
{   /* size: 88 bytes  */
  volatile unsigned int checksum;
  volatile unsigned int checksum_length2;
  volatile unsigned int checksum_length1;
  volatile unsigned int checksum_length0;
  volatile unsigned int constant2;
  volatile unsigned int constant1;
  volatile unsigned int constant0;
  volatile unsigned int c_code;
  volatile unsigned int r_code;
  volatile unsigned int oui2;
  volatile unsigned int oui1;
  volatile unsigned int oui0;
  volatile unsigned int vers;
  volatile unsigned int board2;
  volatile unsigned int board1;
  volatile unsigned int board0;
  volatile unsigned int revis3;
  volatile unsigned int revis2;
  volatile unsigned int revis1;
  volatile unsigned int revis0;
  volatile unsigned int sernum1;
  volatile unsigned int sernum0;
};  

/* Registers address map of CAEN 1720 */
struct c1720_address 
{
  /* 0x0000 */ volatile unsigned int readout_buffer[(0x1000-0x0000)/4];
  /* 0x1000 */          unsigned int dummy1[(0x1080-0x1000)/4];
  /* 0x1080 */ volatile struct c1720_chan chan[8];
  /* 0xnnnn */          unsigned int dummy2[(0x8000-0x1880)/4];
  /* 0x8000 */ volatile unsigned int chan_config;
  /* 0x8004 */ volatile unsigned int config_bitset;
  /* 0x8008 */ volatile unsigned int config_clear;
  /* 0x800C */ volatile unsigned int buffer_org;
  /* 0x8010 */ volatile unsigned int buffer_free;
  /* 0x8014 */          unsigned int dummy3[(0x8100-0x8014)/4];
  /* 0x8100 */ volatile unsigned int acq_ctrl;
  /* 0x8104 */ volatile unsigned int acq_status;
  /* 0x8108 */ volatile unsigned int sw_trigger;
  /* 0x810C */ volatile unsigned int trigmask_enable;
  /* 0x8110 */ volatile unsigned int tmask_out;
  /* 0x8114 */ volatile unsigned int post_trigset;
  /* 0x8118 */ volatile unsigned int fio_data;
  /* 0x811C */ volatile unsigned int fio_ctrl;
  /* 0x8120 */ volatile unsigned int enable_mask;
  /* 0x8124 */ volatile unsigned int firmware;
  /* 0x8128 */ volatile unsigned int downsamp_fact;
  /* 0x812C */ volatile unsigned int event_stored;
  /* 0x8130 */          unsigned int dummy4[(0x8138-0x8130)/4];
  /* 0x8138 */ volatile unsigned int setmon_dac;
  /* 0x813C */          unsigned int dummy5;
  /* 0x8140 */ volatile unsigned int board_info;
  /* 0x8144 */ volatile unsigned int monitor_mode;
  /* 0x8148 */          unsigned int dummy6;
  /* 0x814C */ volatile unsigned int event_size;
  /* 0x8150 */          unsigned int dummy7[(0xEF00-0x8150)/4];
  /* 0xEF00 */ volatile unsigned int vme_ctrl;
  /* 0xEF04 */ volatile unsigned int vme_status;
  /* 0xEF08 */ volatile unsigned int board_id;
  /* 0xEF0C */ volatile unsigned int multi_addrctrl;
  /* 0xEF10 */ volatile unsigned int reloc_addr;
  /* 0xEF14 */ volatile unsigned int interrupt_id;
  /* 0xEF18 */ volatile unsigned int interrupt_num;
  /* 0xEF1C */ volatile unsigned int blt_evnum;
  /* 0xEF20 */ volatile unsigned int scratch;
  /* 0xEF24 */ volatile unsigned int sw_reset;
  /* 0xEF28 */ volatile unsigned int sw_clear;
  /* 0xEF2C */ volatile unsigned int flash_enable;
  /* 0xEF30 */ volatile unsigned int flash_data;
  /* 0xEF34 */ volatile unsigned int config_reload;
  /* 0xEF38 */          unsigned int dummy8[(0xF000-0xEF38)/4];
  /* 0xF000 */ volatile struct c1720_romAddr rom[8];

};

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


#endif /* __V1720LIBH__ */
