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
#define _zeros_ {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0}
static caen1725param_t defparam =
  {
    .external_trigger = 0,
    .fpio_level = 0,
    .record_length = _zeros_,
    .max_tail = _zeros_,
    .gain_factor = _zeros_,
    .pre_trigger = _zeros_,
    .n_lfw = _zeros_,
    .bline_defmode = _zeros_,
    .bline_defvalue = _zeros_,
    .pulse_polarity = _zeros_,
    .test_pulse = 0,
    .tp_type = 0,
    .self_trigger = 0,
    .trg_threshold = _zeros_,
    .enable_input_mask = 0,
    .dc_offset = _zeros_
  };


int32_t
caen1725ConfigInitGlobals()
{
  std::cout << __func__ << ": INFO: here" << std::endl;

  return 0;
}

/**
 * @brief convert a string of 1s and 0s separated by spaces into a bitmask
 * @param[in] bitstring input string
 * @return output mask
 */
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


/**
 * @brief Write the Ini values for the input slot to the local module structure
 * @param[in] slotstring String specifing the slot number "ALLSLOTS", "SLOT3", ...
 */
void
slot2param(std::string slotstring)
{
  caen1725param_t *sp;
  if(ir == NULL)
    return;

  // figure out with slot to fill from the string
  if(slotstring.compare("ALLSLOTS") == 0)
    {
      sp = &all_param;
    }

  // Check for "SLOT" parameter with slotnumber
  int32_t slotID = -1;
  if(sscanf(slotstring.c_str(),"SLOT %d", &slotID) == 1)
    {
      std::cout << "slot = " << slotID << std::endl;
      if((slotID > 2) && (slotID < MAX_VME_SLOTS))
	sp = &param[slotID];
      else
	{
	  std::cerr << __func__ << "(" << slotstring << "): Invalid id = " << slotID << std::endl;
	  return;
	}
    }

  //
  // Module parameters
  //
  std::string var_string = ir->Get(slotstring, "EXTERNAL_TRIGGER","NA");
  if(var_string.compare("NA") == 0)
    {
      sp->external_trigger = defparam.external_trigger;
    }
  else
    {
      // FIXME: check value
      if(var_string.compare("") == 0)
	sp->external_trigger = 1;
      else
	sp->external_trigger = 0;
    }

  std::string fpio_string = ir->Get(slotstring, "FPIO_LEVEL", "NA");
  if(var_string.compare("NA") == 0)
    {
      sp->fpio_level = defparam.fpio_level;
    }
  else
    {
      // FIXME: check value
      if(var_string.compare("TTL") == 0)
	sp->fpio_level = 1;
      else
	sp->fpio_level = 0;
    }

  sp->test_pulse =
    ir->GetInteger(slotstring, "TEST_PULSE", defparam.test_pulse);

  sp->tp_type =
    ir->GetInteger(slotstring, "TP_TYPE", defparam.tp_type);

  sp->self_trigger =
    ir->GetBoolean(slotstring,"SELF_TRIGGER",defparam.self_trigger) ? 1 : 0;

  sp->enable_input_mask =
    string2mask(ir->Get(slotstring, "ENABLE_INPUT_MASK", "0").c_str());

  //
  // Channel parameters
  //
  std::string var_str;
  int32_t ich;

  var_str.clear();
  var_str = "RECORD_LENGTH";

  sp->record_length[CHANNEL_COMMON] =
    ir->GetInteger(slotstring, var_str, defparam.record_length[CHANNEL_COMMON]);

  for(ich = 0; ich < 16; ich++)
    {
      std::string ch_var_str = var_str + "_CHAN" + std::to_string(ich);

      sp->record_length[ich]  =
	ir->GetInteger(slotstring, ch_var_str, sp->record_length[CHANNEL_COMMON]);
    }

  var_str.clear();
  var_str = "GAIN_FACTOR";

  sp->gain_factor[CHANNEL_COMMON] =
    ir->GetInteger(slotstring, var_str, defparam.gain_factor[CHANNEL_COMMON]);

  for(ich = 0; ich < 16; ich++)
    {
      std::string ch_var_str = var_str + "_CHAN" + std::to_string(ich);

      sp->gain_factor[ich]  =
	ir->GetInteger(slotstring, ch_var_str, sp->gain_factor[CHANNEL_COMMON]);
    }

  var_str.clear();
  var_str = "MAX_TAIL";

  sp->max_tail[CHANNEL_COMMON] =
    ir->GetInteger(slotstring, var_str, defparam.max_tail[CHANNEL_COMMON]);

  for(ich = 0; ich < 16; ich++)
    {
      std::string ch_var_str = var_str + "_CHAN" + std::to_string(ich);

      sp->max_tail[ich]  =
	ir->GetInteger(slotstring, ch_var_str, sp->max_tail[CHANNEL_COMMON]);
    }

  var_str.clear();
  var_str = "PRE_TRIGGER";

  sp->pre_trigger[CHANNEL_COMMON] =
    ir->GetInteger(slotstring, var_str, defparam.pre_trigger[CHANNEL_COMMON]);

  for(ich = 0; ich < 16; ich++)
    {
      std::string ch_var_str = var_str + "_CHAN" + std::to_string(ich);

      sp->pre_trigger[ich]  =
	ir->GetInteger(slotstring, ch_var_str, sp->pre_trigger[CHANNEL_COMMON]);
    }

  var_str.clear();
  var_str = "N_LFW";

  sp->n_lfw[CHANNEL_COMMON] =
    ir->GetInteger(slotstring, var_str, defparam.n_lfw[CHANNEL_COMMON]);

  for(ich = 0; ich < 16; ich++)
    {
      std::string ch_var_str = var_str + "_CHAN" + std::to_string(ich);

      sp->n_lfw[ich]  =
	ir->GetInteger(slotstring, ch_var_str, sp->n_lfw[CHANNEL_COMMON]);
    }

  var_str.clear();
  var_str = "BLINE_DEFMODE";

  bool var_bool = true;
  sp->bline_defmode[CHANNEL_COMMON] =
    ir->GetBoolean(slotstring, var_str, defparam.bline_defmode[CHANNEL_COMMON]);
  for(ich = 0; ich < 16; ich++)
    {
      std::string ch_var_str = var_str + "_CHAN" + std::to_string(ich);

      sp->bline_defmode[ich]  =
	ir->GetBoolean(slotstring, ch_var_str, sp->bline_defmode[CHANNEL_COMMON]);
    }

  var_str.clear();
  var_str = "BLINE_DEFVALUE";

  sp->bline_defvalue[CHANNEL_COMMON] =
    ir->GetInteger(slotstring, var_str, defparam.bline_defvalue[CHANNEL_COMMON]);

  for(ich = 0; ich < 16; ich++)
    {
      std::string ch_var_str = var_str + "_CHAN" + std::to_string(ich);

      sp->bline_defvalue[ich]  =
	ir->GetInteger(slotstring, ch_var_str, sp->bline_defvalue[CHANNEL_COMMON]);
    }

  var_str.clear();
  var_str = "PULSE_POLARITY";
  // FIXME: check value matches polarity
  sp->pulse_polarity[CHANNEL_COMMON] =
    ir->GetInteger(slotstring, var_str, defparam.pulse_polarity[CHANNEL_COMMON]);

  for(ich = 0; ich < 16; ich++)
    {
      std::string ch_var_str = var_str + "_CHAN" + std::to_string(ich);

      sp->pulse_polarity[ich]  =
	ir->GetInteger(slotstring, ch_var_str, sp->pulse_polarity[CHANNEL_COMMON]);
    }


  var_str.clear();
  var_str = "TRG_THRESHOLD";

  sp->trg_threshold[CHANNEL_COMMON] =
    ir->GetInteger(slotstring, var_str, defparam.trg_threshold[CHANNEL_COMMON]);

  for(ich = 0; ich < 16; ich++)
    {
      std::string ch_var_str = var_str + "_CHAN" + std::to_string(ich);

      sp->trg_threshold[ich]  =
	ir->GetInteger(slotstring, ch_var_str, sp->trg_threshold[CHANNEL_COMMON]);
    }

  var_str.clear();
  var_str = "DC_OFFSET";

  sp->dc_offset[CHANNEL_COMMON] =
    ir->GetInteger(slotstring, var_str, defparam.dc_offset[CHANNEL_COMMON]);

  for(ich = 0; ich < 16; ich++)
    {
      std::string ch_var_str = var_str + "_CHAN" + std::to_string(ich);

      sp->dc_offset[ich]  =
	ir->GetInteger(slotstring, ch_var_str, sp->dc_offset[CHANNEL_COMMON]);
    }


  // fill the defaults with ALLSLOTS
  if(slotstring.compare("ALLSLOTS") == 0)
    {
      memcpy(&defparam, &all_param, sizeof(caen1725param_t));
    }

}

/**
 * @brief Print the values stored in the local structure for specified slot
 * @param[in] id slot id
 */
void
caen1725ConfigPrintParameters(uint32_t id)
{
  caen1725param_t *sp;

  if(id == 0)
    sp = &all_param;
  else if (id < (MAX_VME_SLOTS + 1))
    sp = &param[id];
  else
    {
      std::cerr << __func__ << "ERROR: Invalid id " << id << std::endl;
    }

#ifndef PRINTPARAM
#define PRINTPARAM(_reg)						\
  printf("  %22.18s = 0x%08x (%d)\n",					\
	 #_reg, sp->_reg, sp->_reg);
#endif
#define PRINTCH(_reg)							\
  for(int32_t _ich = 0; _ich < CHANNEL_COMMON; _ich++)			\
    printf("  %18.18s[%2d] = 0x%08x (%d)%s",				\
	   #_reg, _ich, sp->_reg[_ich], sp->_reg[_ich],			\
	   (_ich % 2)==1 ? "\n" : "\t");

  printf("%s: id = %d\n", __func__, id);

  PRINTPARAM(external_trigger);
  PRINTPARAM(fpio_level);
  PRINTPARAM(test_pulse);
  PRINTPARAM(tp_type);
  PRINTPARAM(self_trigger);
  PRINTPARAM(enable_input_mask);

  PRINTCH(record_length);
  PRINTCH(gain_factor);
  PRINTCH(max_tail);
  PRINTCH(pre_trigger);
  PRINTCH(n_lfw);
  PRINTCH(bline_defmode);
  PRINTCH(bline_defvalue);
  PRINTCH(pulse_polarity);
  PRINTCH(trg_threshold);
  PRINTCH(dc_offset);

}

int32_t
param2caen(int32_t id)
{
  /* Write the parameters to the device */

  /* External Trigger */

  /* fpio level */

  /* record length */

  return 0;
}

int32_t
caen1725ConfigLoadParameters()
{
  if(ir == NULL)
    return 1;

  /* Handle the ALLSLOTS section first (defaults for reset of crate) */
  std::string current_slot = "ALLSLOTS";

  std::set<std::string> sections = ir->Sections();
  std::set<std::string>::iterator it = sections.find(current_slot);

  if(it != sections.end())
    {
      slot2param(current_slot);
    }

  /* Loop through the others */
  for(it=sections.begin(); it!=sections.end(); ++it)
    {
      if(it->compare("ALLSLOTS") == 0) continue;

      slot2param(*it);
    }

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
