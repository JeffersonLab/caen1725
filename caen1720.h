/* 
   caen1720.h. 
   Header for the CAEN 1720 FADC 
   Includes define statements and memory map.

   R. Michaels, Apr 2008   */

#define C1720_MAX_BOARDS         8
#define C1720_MAX_ADC_CHANNELS   8


/* Infomation related to each channel (in address map below) */
struct c1720_chan {  /* 64 long words, 256 bytes */
      volatile unsigned long thresh;             /* 0x1n80   for chan n*/
      volatile unsigned long time_overunder;     /* 0x1n84   */
      volatile unsigned long status;             /* 0x1n88   */
      volatile unsigned long fpga_firmware;      /* 0x1n8c   */
      volatile unsigned long dummy1;            
      volatile unsigned long buffer_occupancy;   /* 0x1n94   */
      volatile unsigned long dac;                /* 0x1n98   */
      volatile unsigned long adc_config;         /* 0x1n9c   */
      unsigned long dummy2[56];
};


/* Configuration ROM  (in address map below) */
struct c1720_romAddr {   /* size: 88 bytes  */
      volatile unsigned long checksum;
      volatile unsigned long checksum_length2;
      volatile unsigned long checksum_length1;
      volatile unsigned long checksum_length0;
      volatile unsigned long constant2;
      volatile unsigned long constant1;
      volatile unsigned long constant0;
      volatile unsigned long c_code;
      volatile unsigned long r_code;
      volatile unsigned long oui2;
      volatile unsigned long oui1;
      volatile unsigned long oui0;
      volatile unsigned long vers;
      volatile unsigned long board2;
      volatile unsigned long board1;
      volatile unsigned long board0;
      volatile unsigned long revis3;
      volatile unsigned long revis2;
      volatile unsigned long revis1;
      volatile unsigned long revis0;
      volatile unsigned long sernum1;
      volatile unsigned long sernum0;
};  

/* Registers address map of CAEN 1720 */
struct c1720_address {
      unsigned long readout_buffer[1024];
      unsigned long dummy1[32];
      volatile struct c1720_chan chan[8];  /*  0x1080   */
      unsigned long dummy2[6624];
      unsigned long chan_config;           /*  0x8000   */  
      unsigned long config_bitset;         /*  0x8004   */  
      unsigned long config_clear;          /*  0x8008   */  
      unsigned long buffer_org;            /*  0x800c   */  
      unsigned long buffer_free;           /*  0x8010   */  
      unsigned long dummy3[59];
      unsigned long acq_ctrl;              /*  0x8100   */  
      unsigned long acq_status;            /*  0x8104   */  
      unsigned long sw_trigger;            /*  0x8108   */  
      unsigned long trigmask_enable;       /*  0x810c   */  
      unsigned long tmask_out;             /*  0x8110   */  
      unsigned long post_trigset;          /*  0x8114   */  
      unsigned long fio_data;              /*  0x8118   */  
      unsigned long fio_ctrl;              /*  0x811c   */  
      unsigned long enable_mask;           /*  0x8120   */  
      unsigned long firmware;              /*  0x8124   */  
      unsigned long downsamp_fact;         /*  0x8128   */  
      unsigned long event_stored;          /*  0x812c   */  
      unsigned long dummy4[2];
      unsigned long setmon_dac;            /*  0x8138   */  
      unsigned long dummy5[1];
      unsigned long board_info;            /*  0x8140   */  
      unsigned long monitor_mode;          /*  0x8144   */  
      unsigned long dummy6;                /*  0x8148   */
      unsigned long event_size;            /*  0x814c   */
      unsigned long dummy7[7020];
      unsigned long vme_ctrl;              /*  0xef00   */  
      unsigned long vme_status;            /*  0xef04   */  
      unsigned long board_id;              /*  0xef08   */  
      unsigned long multi_addrctrl;        /*  0xef0c   */  
      unsigned long reloc_addr;            /*  0xef10   */  
      unsigned long interrupt_id;          /*  0xef14   */  
      unsigned long interrupt_num;         /*  0xef18   */  
      unsigned long blt_evnum;             /*  0xef1c   */  
      unsigned long scratch;               /*  0xef20   */  
      unsigned long sw_reset;              /*  0xef24   */  
      unsigned long sw_clear;              /*  0xef28   */  
      unsigned long flash_enable;          /*  0xef2c   */  
      unsigned long flash_data;            /*  0xef30   */  
      unsigned long config_reload;         /*  0xef34   */  
      unsigned long dummy8[50];
      volatile struct c1720_romAddr rom[8];/*  0xf000   */

};
