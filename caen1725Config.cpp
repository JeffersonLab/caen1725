#include <cstring>
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include "caen1725Config.h"
#include "INIReader.h"
#include "caen1725Lib.h"

// place to store the ini INIReader instance
INIReader *ir;

static caen1725param_t param[MAX_VME_SLOTS+1];
static caen1725param_t all_param;
static caen1725param_t defparam =
  {
    .external_trigger = 0,
    .fpio_level = 0,
    .record_length = 0,
    .max_tail = 0,
    .gain_factor = 0,
    .pre_trigger = 0,
    .n_lfw = 0,
    .bline_defmode = 0,
    .bline_defvalue = 0,
    .pulse_polarity = 0,
    .test_pulse = 0,
    .tp_type = 0,
    .self_trigger = 0,
    .trg_threshold = 0,
    .enable_input_mask = 0,
    .dc_offset = {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0}
  };


int32_t
caen1725ConfigInitGlobals()
{
  std::cout << __func__ << ": INFO: here" << std::endl;

  return 0;
}

std::string sections(INIReader &reader)
{
    std::stringstream ss;
    std::set<std::string> sections = reader.Sections();
    for (std::set<std::string>::iterator it = sections.begin(); it != sections.end(); ++it)
        ss << *it << ",";
    return ss.str();
}

uint32_t
string2mask(const char* bitstring)
{
  uint32_t outmask = 0, ibit = 0;

  char input[256];
  strncpy(input, bitstring, 256);

  const char *delim = " ";
  char *token = std::strtok(input, delim);

  while(token)
    {
      if(strcmp(token, "1") == 0)
	outmask |= (1 << ibit);

      ibit++;
      if(ibit > 31)
	{
	  std::cerr << "too many characters in bit string" << std::endl;
	  return outmask;
	}
      token = std::strtok(nullptr, delim);
    }

  return outmask;
}

void
slot2param(std::string slotstring)
{
  caen1725param_t *sp;
  if(ir == NULL)
    return;

  // figure out with slot to fill from the string
  if(slotstring.compare("ALLSLOTS") == 0)
    sp = &all_param;

  sp->external_trigger =
    (ir->Get(slotstring, "EXTERNAL_TRIGGER","ACQUISITION_ONLY").compare("ACQUISITION_ONLY") == 0) ? 1 : 0;

  sp->fpio_level =
    (ir->Get(slotstring, "FPIO_LEVEL", "TTL").compare("TTL") == 0) ? 1 : 0;

  sp->record_length =
    ir->GetInteger(slotstring, "RECORD_LENGTH", defparam.record_length);

  sp->max_tail =
    ir->GetInteger(slotstring, "MAX_TAIL", defparam.max_tail);

  sp->gain_factor =
    ir->GetInteger(slotstring, "GAIN_FACTOR", defparam.gain_factor);

  sp->pre_trigger =
    ir->GetInteger(slotstring, "PRE_TRIGGER", defparam.pre_trigger);

  sp->n_lfw =
    ir->GetInteger(slotstring, "N_LFW", defparam.n_lfw);

  sp->bline_defmode =
    (ir->Get(slotstring, "BLINE_DEFMODE", "NO").compare("YES") == 0) ? 1 : 0;

  sp->bline_defvalue =
    ir->GetInteger(slotstring, "BLINE_DEFVALUE", defparam.bline_defvalue);

  sp->pulse_polarity =
    (ir->Get(slotstring, "PULSE_POLARITY", "NEGATIVE").compare("POSITIVE") == 0) ? 1 : 0;

  sp->test_pulse =
    ir->GetInteger(slotstring, "TEST_PULSE", defparam.test_pulse);

  sp->tp_type =
    ir->GetInteger(slotstring, "TP_TYPE", defparam.tp_type);

  sp->trg_threshold =
    ir->GetInteger(slotstring, "TRG_THRESHOLD", defparam.trg_threshold);

  sp->enable_input_mask =
    string2mask(ir->Get(slotstring, "ENABLE_INPUT_MASK", "0").c_str());

  for(int32_t idc = 0; idc < 16; idc++)
    {
      std::string dcstring = "DC_OFFSET_CHAN" + std::to_string(idc);

      sp->dc_offset[idc]  =
	ir->GetInteger(slotstring, dcstring, defparam.dc_offset[idc]);
    }
}

void
caen1725ConfigPrintParameters(uint32_t id)
{
  caen1725param_t *sp = &all_param;

#ifndef PRINTPARAM
#define PRINTPARAM(_reg)						\
  printf("  %20s = 0x%08x (%d)\n",					\
	 #_reg, sp->_reg, sp->_reg);
#endif
  printf("%s: id = %d\n", __func__, id);

  PRINTPARAM(external_trigger);
  PRINTPARAM(fpio_level);
  PRINTPARAM(record_length);
  PRINTPARAM(max_tail);
  PRINTPARAM(gain_factor);
  PRINTPARAM(pre_trigger);
  PRINTPARAM(n_lfw);
  PRINTPARAM(bline_defmode);
  PRINTPARAM(bline_defvalue);
  PRINTPARAM(pulse_polarity);
  PRINTPARAM(test_pulse);
  PRINTPARAM(tp_type);
  PRINTPARAM(self_trigger);
  PRINTPARAM(trg_threshold);
  PRINTPARAM(enable_input_mask);

  PRINTPARAM(dc_offset[0]);
  PRINTPARAM(dc_offset[1]);
  PRINTPARAM(dc_offset[2]);
  PRINTPARAM(dc_offset[3]);
  PRINTPARAM(dc_offset[4]);
  PRINTPARAM(dc_offset[5]);
  PRINTPARAM(dc_offset[6]);
  PRINTPARAM(dc_offset[7]);
  PRINTPARAM(dc_offset[8]);
  PRINTPARAM(dc_offset[9]);
  PRINTPARAM(dc_offset[10]);
  PRINTPARAM(dc_offset[11]);
  PRINTPARAM(dc_offset[12]);
  PRINTPARAM(dc_offset[13]);
  PRINTPARAM(dc_offset[14]);
  PRINTPARAM(dc_offset[15]);

}

int32_t
caen1725ConfigLoadParameters()
{
  if(ir == NULL)
    return 1;

  /* Fill all slots with ALLSLOTS parameters */
  std::string current_slot = "ALLSLOTS";

  slot2param(current_slot);


  return 0;
}

// load in parameters to structure from filename
int32_t
caen1725Config(const char *filename)
{
  std::cout << __func__ << ": INFO: here" << std::endl;

  ir = new INIReader(filename);
  if(ir->ParseError() < 0)
    {
      std::cout << "Can't load: " << filename << std::endl;
      return 1;
    }

  std::cout << "Found sections : " << sections(*ir) << std::endl;
  caen1725ConfigLoadParameters();
  return 0;
}

// destroy the ini object
int32_t
caen1725ConfigFree()
{
  std::cout << __func__ << ": INFO: here" << std::endl;

  if(ir == NULL)
    return 1;

  std::cout << "delete ir" << std::endl;
  delete ir;

  return 0;
}
