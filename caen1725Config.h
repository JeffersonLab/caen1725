#pragma once
#include <stdint.h>
#include "caen1725Lib.h"
#ifdef __cplusplus
extern "C" {
#endif

  // added index is the 'common' value
  #define CHANNEL_COMMON C1725_MAX_ADC_CHANNELS
  typedef struct
  {
    int32_t external_trigger;
    int32_t fpio_level;
    int32_t record_length[C1725_MAX_ADC_CHANNELS+1];
    int32_t max_tail[C1725_MAX_ADC_CHANNELS+1];
    int32_t gain_factor[C1725_MAX_ADC_CHANNELS+1];
    int32_t pre_trigger[C1725_MAX_ADC_CHANNELS+1];
    int32_t n_lfw[C1725_MAX_ADC_CHANNELS+1];
    int32_t bline_defmode[C1725_MAX_ADC_CHANNELS+1];
    int32_t bline_defvalue[C1725_MAX_ADC_CHANNELS+1];
    int32_t pulse_polarity[C1725_MAX_ADC_CHANNELS+1];
    int32_t test_pulse;
    int32_t tp_type;
    int32_t self_trigger;
    int32_t trg_threshold[C1725_MAX_ADC_CHANNELS+1];
    uint16_t enable_input_mask;
    int32_t dc_offset[C1725_MAX_ADC_CHANNELS+1];
  } caen1725param_t;

  /* routine prototypes */
  int32_t caen1725ConfigInitGlobals();
  int32_t caen1725Config(const char *filename);
  int32_t caen1725ConfigFree();
  void    caen1725ConfigPrintParameters(uint32_t id);
#ifdef __cplusplus
}
#endif
