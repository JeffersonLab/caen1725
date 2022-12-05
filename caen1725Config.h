#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

  typedef struct
  {
    int32_t external_trigger;
    int32_t fpio_level;
    int32_t record_length;
    int32_t max_tail;
    int32_t gain_factor;
    int32_t pre_trigger;
    int32_t n_lfw;
    int32_t bline_defmode;
    int32_t bline_defvalue;
    int32_t pulse_polarity;
    int32_t test_pulse;
    int32_t tp_type;
    int32_t self_trigger;
    int32_t trg_threshold;
    uint16_t enable_input_mask;
    int32_t dc_offset[16];
  } caen1725param_t;

  /* routine prototypes */
  int32_t caen1725ConfigInitGlobals();
  int32_t caen1725Config(const char *filename);
  int32_t caen1725ConfigFree();
  void    caen1725ConfigPrintParameters(uint32_t id);
#ifdef __cplusplus
}
#endif
