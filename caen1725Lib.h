#pragma once
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
 * @file      caen1725Lib.h
 * @brief     Header for library for the CAEN 1725 Digitizer - DPP-DAW
 *
 */
#include <stdint.h>
#include "jvme.h"

/* Automatically generate unique blank register names */
#ifdef __COUNTER__
#define _BLANK JOIN(_blank, __COUNTER__)
#else
#define _BLANK JOIN(_blank, __LINE__)
#endif
#define JOIN(x,y) _DO_JOIN(x,y)
#define _DO_JOIN(x,y) x##y

#ifndef MAX_VME_SLOTS
/** This is either 20 or 21 */
#define MAX_VME_SLOTS 21
#endif

#define C1725_MAX_BOARDS         (MAX_VME_SLOTS-1)
#define C1725_MAX_ADC_CHANNELS   16

/* Board ID as obtained from configuration rom
   = (board0<<16) | (board1<<8) | (board2) */
#define C1725_BOARD_ID      0xB80600
#define C1725_BOARD_ID_MASK 0xFFFFFE

/* Infomation related to each channel (in address map below) */
typedef struct
{
  /* 0x1n00          */ uint32_t _BLANK[(0x1020-0x1000)/4];

  /* 0x1n20 */ volatile uint32_t minimum_record_length;
  /* 0x1n24          */ uint32_t _BLANK;
  /* 0x1n28 */ volatile uint32_t input_dynamic_range;
  /* 0x1n2C          */ uint32_t _BLANK[(0x1034-0x102C)/4];

  /* 0x1n34 */ volatile uint32_t input_delay;
  /* 0x1n38 */ volatile uint32_t pre_trigger;
  /* 0x1n3C            */ uint32_t _BLANK[(0x1060-0x103C)/4];

  /* 0x1n60 */ volatile uint32_t trigger_threshold;
  /* 0x1n64 */ volatile uint32_t fixed_baseline;
  /* 0x1n68 */ volatile uint32_t couple_trigger_logic;
  /* 0x1n6C          */ uint32_t _BLANK[(0x1078-0x106C)/4];

  /* 0x1n78 */ volatile uint32_t samples_under_threshold;
  /* 0x1n7c */ volatile uint32_t maximum_tail;

  /* 0x1n80 */ volatile uint32_t dpp_algorithm_ctrl;
  /* 0x1n84 */ volatile uint32_t couple_over_threshold_trigger;
  /* 0x1n88 */ volatile uint32_t status;
  /* 0x1n8C */ volatile uint32_t firmware_revision;
  /* 0x1n90          */ uint32_t _BLANK[(0x1098-0x1090)/4];

  /* 0x1n98 */ volatile uint32_t dc_offset;
  /* 0x1n9C          */ uint32_t _BLANK[(0x10A8-0x109C)/4];

  /* 0x1nA8 */ volatile uint32_t adc_temperature;
  /* 0x1nAC          */ uint32_t _BLANK[(0x1100-0x10AC)/4];
}  c1725_chan;


/* Configuration ROM  (in address map below) */
typedef struct
{
  /* 0xF000 */ volatile uint32_t checksum;
  /* 0xF004 */ volatile uint32_t checksum2;
  /* 0xF008 */ volatile uint32_t checksum1;
  /* 0xF00C */ volatile uint32_t checksum0;
  /* 0xF010 */ volatile uint32_t constant2;
  /* 0xF014 */ volatile uint32_t constant1;
  /* 0xF018 */ volatile uint32_t constant0;
  /* 0xF01C */ volatile uint32_t c_code;
  /* 0xF020 */ volatile uint32_t r_code;
  /* 0xF024 */ volatile uint32_t oui2;
  /* 0xF028 */ volatile uint32_t oui1;
  /* 0xF02C */ volatile uint32_t oui0;
  /* 0xF030 */ volatile uint32_t vers;
  /* 0xF034 */ volatile uint32_t form_factor;
  /* 0xF038 */ volatile uint32_t board1;
  /* 0xF03C */ volatile uint32_t board0;
  /* 0xF040 */ volatile uint32_t revis3;
  /* 0xF044 */ volatile uint32_t revis2;
  /* 0xF048 */ volatile uint32_t revis1;
  /* 0xF04C */ volatile uint32_t revis0;
  /* 0xF050 */ volatile uint32_t flash_type;
  /* 0xF054          */ uint32_t _BLANK[(0xF080-0xF054)/4];
  /* 0xF080 */ volatile uint32_t sernum1;
  /* 0xF084 */ volatile uint32_t sernum0;
  /* 0xF088 */ volatile uint32_t vcxo_type;
}  c1725_romAddr;

/* Registers address map of CAEN 1725 */
typedef struct
{
  /* 0x0000 */ volatile uint32_t readout_buffer[(0x1000-0x0000)/4];

  /* 0x1000 */ volatile c1725_chan chan[C1725_MAX_ADC_CHANNELS];

  /* 0x2000         */ uint32_t _BLANK[(0x8000-0x2000)/4];

  /* 0x8000 */ volatile uint32_t config;
  /* 0x8004 */ volatile uint32_t config_bitset;
  /* 0x8008 */ volatile uint32_t config_bitclear;
  /* 0x800C */ volatile uint32_t buffer_org;
  /* 0x8010          */ uint32_t _BLANK[(0x809C-0x8010)/4];

  /* 0x809C */ volatile uint32_t channel_adc_calibration;
  /* 0x80A0          */ uint32_t _BLANK[(0x8100-0x80A0)/4];

  /* 0x8100 */ volatile uint32_t acq_ctrl;
  /* 0x8104 */ volatile uint32_t acq_status;
  /* 0x8108 */ volatile uint32_t sw_trigger;
  /* 0x810C */ volatile uint32_t global_trigger_mask;

  /* 0x8110 */ volatile uint32_t fp_trg_out_enable_mask;
  /* 0x8114 */ volatile uint32_t _BLANK;
  /* 0x8118 */ volatile uint32_t lvds_io_data;
  /* 0x811C */ volatile uint32_t fp_io_ctrl;

  /* 0x8120 */ volatile uint32_t channel_enable_mask;
  /* 0x8124 */ volatile uint32_t roc_firmware_revision;
  /* 0x8128          */ uint32_t _BLANK;
  /* 0x812C */ volatile uint32_t event_stored;

  /* 0x8130          */ uint32_t _BLANK[(0x8138-0x8130)/4];
  /* 0x8138 */ volatile uint32_t voltage_level_mode_config;
  /* 0x813C */ volatile uint32_t software_clock_sync;

  /* 0x8140 */ volatile uint32_t board_info;
  /* 0x8144 */ volatile uint32_t analog_monitor_mode;
  /* 0x8148          */ uint32_t _BLANK;
  /* 0x814C */ volatile uint32_t event_size;
  /* 0x8150          */ uint32_t _BLANK[(0x8168-0x8150)/4];

  /* 0x8168 */ volatile uint32_t fan_speed_ctrl;
  /* 0x816C          */ uint32_t _BLANK;

  /* 0x8170 */ volatile uint32_t run_start_stop_delay;
  /* 0x8174          */ uint32_t _BLANK;

  /* 0x8178 */ volatile uint32_t board_failure_status;
  /* 0x817C          */ uint32_t _BLANK[(0x81A0-0x817C)/4];

  /* 0x81A0 */ volatile uint32_t lvds_io_csr;
  /* 0x81A4          */ uint32_t _BLANK[(0x81C4-0x81A4)/4];

  /* 0x81C4 */ volatile uint32_t extended_veto_delay;
  /* 0x81C8          */ uint32_t _BLANK[(0xEF00-0x81C8)/4];

  /* 0xEF00 */ volatile uint32_t readout_ctrl;
  /* 0xEF04 */ volatile uint32_t readout_status;
  /* 0xEF08 */ volatile uint32_t board_id;
  /* 0xEF0C */ volatile uint32_t multicast_address;

  /* 0xEF10 */ volatile uint32_t relocation_address;
  /* 0xEF14 */ volatile uint32_t interrupt_id;
  /* 0xEF18 */ volatile uint32_t interrupt_num;
  /* 0xEF1C */ volatile uint32_t max_events_per_blt;

  /* 0xEF20 */ volatile uint32_t scratch;
  /* 0xEF24 */ volatile uint32_t software_reset;
  /* 0xEF28 */ volatile uint32_t software_clear;
  /* 0xEF2C          */ uint32_t _BLANK[(0xEF34-0xEF2C)/4];

  /* 0xEF34 */ volatile uint32_t config_reload;
  /* 0xEF38          */ uint32_t _BLANK[(0xF000-0xEF38)/4];

  /* 0xF000 */ volatile c1725_romAddr rom;

} c1725_address;

/* chan_config masks and bits */
#define C1725_CHAN_CONFIG_TRIG_OVERLAP            (1<<1)
#define C1725_CHAN_CONFIG_TEST_PATTERN            (1<<3)
#define C1725_CHAN_CONFIG_MEM_SEQUENTIAL          (1<<4)
#define C1725_CHAN_CONFIG_TRIGOUT_UNDER_THRESHOLD (1<<6)
#define C1725_CHAN_CONFIG_PACK2_5                 (1<<11)
#define C1725_CHAN_CONFIG_ZERO_SUPPRESSION_MASK   0x000F0000
#define C1725_CHAN_CONFIG_ZLE                     (1<<16)
#define C1725_CHAN_CONFIG_ZS_AMP                  ((1<<16)|(1<<17))

/* trigmask_enable masks and bits */
#define C1725_TRIGMASK_ENABLE_SOFTWARE         (1<<31)
#define C1725_TRIGMASK_ENABLE_EXTERNAL         (1<<30)
#define C1725_TRIGMASK_ENABLE_COINC_LEVEL_MASK 0x07000000
#define C1725_TRIGMASK_ENABLE_CHANNEL_MASK     0x000000FF

/* enable_mask mask */
#define C1725_ENABLE_CHANNEL_MASK     0x000000FF


/* Source options for c1725EnableTriggerSource/c1725DisableTriggerSource */
#define C1725_SOFTWARE_TRIGGER_ENABLE 0
#define C1725_EXTERNAL_TRIGGER_ENABLE 1
#define C1725_CHANNEL_TRIGGER_ENABLE  2
#define C1725_ALL_TRIGGER_ENABLE      3

/* acq_status Masks and bits */
#define C1725_ACQ_STATUS_RUN_ENABLED  (1<<2)
#define C1725_ACQ_STATUS_EVENT_READY  (1<<3)
#define C1725_ACQ_STATUS_EVENT_FULL   (1<<4)
#define C1725_ACQ_STATUS_CLK_EXTERNAL (1<<5)
#define C1725_ACQ_STATUS_PLL_BYPASS   (1<<6)
#define C1725_ACQ_STATUS_PLL_LOCKED   (1<<7)
#define C1725_ACQ_STATUS_ACQ_READY    (1<<8)

/* vme_ctrl Masks and bits */
#define C1725_VME_CTRL_INTLEVEL_MASK   0x7
#define C1725_VME_CTRL_BERR_ENABLE    (1<<4)
#define C1725_VME_CTRL_ALIGN64_ENABLE (1<<5)
#define C1725_VME_CTRL_RELOC_ENABLE   (1<<6)
#define C1725_VME_CTRL_ROAK_ENABLE    (1<<7)

/* vme_status Masks and bits */
#define C1725_VME_STATUS_EVENT_READY        (1<<0)
#define C1725_VME_STATUS_OUTPUT_BUFFER_FULL (1<<1)
#define C1725_VME_STATUS_BERR_OCCURRED      (1<<2)

/* monitor_mode bits*/
#define C1725_MONITOR_MODE_MASK       0x7
#define C1725_MONITOR_MODE_MAJORITY   0
#define C1725_MONITOR_MODE_WAVEFORM   1
#define C1725_MONITOR_MODE_BUFFER_OCC 3
#define C1725_MONITOR_MODE_VOLT_LEVEL 4

#define C1725_MONITOR_DAC_MASK 0xFFF

/* Channel specific regs masks and bits */
#define C1725_CHANNEL_THRESHOLD_MASK  0x00000FFF

#define C1725_CHANNEL_TIME_OVERUNDER_MASK 0x00000FFF

#define C1725_CHANNEL_STATUS_MEM_FULL     (1<<0)
#define C1725_CHANNEL_STATUS_MEM_EMPY     (1<<1)
#define C1725_CHANNEL_STATUS_BUSY         (1<<2)
#define C1725_CHANNEL_STATUS_BUFFER_ERROR (1<<5)



/* Event structure masks and bits */
/* Header: 1st word */
#define C1725_HEADER_TYPE_MASK        0xF0000000
#define C1725_HEADER_TYPE_ID          0xA0000000
#define C1725_HEADER_EVENTSIZE_MASK   0x0FFFFFFF
/* Header: 2nd word */
#define C1725_HEADER_BOARDID_MASK     0xF8000000
#define C1725_HEADER_ZLE_FORMAT       (1<<24)
#define C1725_HEADER_BIT_PATTERN_MASK 0x00FFFF00
#define C1725_HEADER_CHANNEL_MASK     0x000000FF
/* Header: 3rd word */
#define C1725_HEADER_EVENT_CNT_MASK   0x00FFFFFF
/* Header: 4th word */
#define C1725_HEADER_TRIGTIME_MASK    0xFFFFFFFF



/* Function prototypes */
int32_t c1725CheckAddresses();
int32_t c1725Init(uint32_t addr, uint32_t addr_inc, int32_t nadc);
int32_t c1725PrintChanStatus(int32_t id, int32_t chan);
int32_t c1725PrintStatus(int32_t id);
int32_t c1725Reset(int32_t id);
int32_t c1725Clear(int32_t id);
int32_t c1725SoftTrigger(int32_t id);
int32_t c1725SetTriggerOverlapping(int32_t id, int32_t enable);
int32_t c1725SetTestPatternGeneration(int32_t id, int32_t enable);
int32_t c1725SetTriggerOnUnderThreshold(int32_t id, int32_t enable);
int32_t c1725SetPack2_5(int32_t id, int32_t enable);
int32_t c1725SetZeroLengthEncoding(int32_t id, int32_t enable);
int32_t c1725SetAmplitudeBasedFullSuppression(int32_t id, int32_t enable);
int32_t c1725EnableTriggerSource(int32_t id, int32_t src, int32_t chanmask, int32_t level);
int32_t c1725DisableTriggerSource(int32_t id, int32_t src, int32_t chanmask);
int32_t c1725EnableFPTrigOut(int32_t id, int32_t src, int32_t chanmask);
int32_t c1725DisableFPTrigOut(int32_t id, int32_t src, int32_t chanmask);
int32_t c1725SetEnableChannelMask(int32_t id, int32_t chanmask);
uint32_t c1725GetEventSize(int32_t id);
uint32_t c1725GetNumEv(int32_t id);
int32_t c1725SetChannelDAC(int32_t id, int32_t chan, int32_t dac);
int32_t c1725BufferFree(int32_t id, int32_t num);
int32_t c1725SetAcqCtrl(int32_t id, int32_t bits);
int32_t c1725BoardReady(int32_t id);
int32_t c1725EventReady(int32_t id);
int32_t c1725SetBufOrg(int32_t id, int32_t code);
int32_t c1725SetBusError(int32_t id, int32_t enable);
int32_t c1725SetAlign64(int32_t id, int32_t enable);
int32_t c1725SetChannelThreshold(int32_t id, int32_t chan, int32_t thresh);
int32_t c1725SetChannelTimeOverUnder(int32_t id, int32_t chan, int32_t samp);
int32_t c1725SetMonitorMode(int32_t id, int32_t mode);
int32_t c1725SetMonitorDAC(int32_t id, int32_t dac);
int32_t c1725SetupInterrupt(int32_t id, int32_t level, int32_t vector);
int32_t c1725EnableInterrupts(int32_t id);
int32_t c1725DisableInterrupts(int32_t id);

int32_t c1725ReadEvent(int32_t id, volatile uint32_t *data, int32_t nwrds, int32_t rflag);
