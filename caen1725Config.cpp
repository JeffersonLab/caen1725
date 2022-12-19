#include <cstring>
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include "caen1725Config.h"
#include "INIReader.h"
#include "caen1725Lib.h"

// debug flag
static bool configDebug = false;


// place to store the ini INIReader instance
INIReader *ir;

static caen1725param_t param[MAX_VME_SLOTS+1];
static caen1725param_t all_param;
#define _zeros_ {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0}
static caen1725param_t defparam =
  {
    .external_trigger = 0,
    .test_pulse = 0,
    .tp_type = 0,
    .self_trigger = 0,
    .fpio_level = 0,
    .enable_input_mask = 0,
    .max_events_per_blt = 0,
    .record_length = _zeros_,
    .gain_factor = _zeros_,
    .pre_trigger = _zeros_,
    .trg_threshold = _zeros_,
    .bline_defmode = _zeros_,
    .bline_defvalue = _zeros_,
    .pulse_polarity = _zeros_,
    .max_tail = _zeros_,
    .dc_offset = _zeros_,
    .n_lfw = _zeros_
  };


int32_t
caen1725ConfigInitGlobals()
{
  if(configDebug)
    std::cout << __func__ << ": INFO: here" << std::endl;

  for(int32_t ip = 0; ip < MAX_VME_SLOTS; ip++)
    {
      memcpy(&param[ip], &defparam, sizeof(caen1725param_t));
    }

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
      if(configDebug)
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

#define _CHANNEL_SEARCH(_var, _param)		\
  var_str.clear();				\
  var_str = _var;			\
  sp->_param[CHANNEL_COMMON] =					\
    ir->GetInteger(slotstring, var_str, defparam._param[CHANNEL_COMMON]); \
  for(ich = 0; ich < 16; ich++)						\
    {									\
      std::string ch_var_str = var_str + "_CHAN" + std::to_string(ich);	\
      sp->_param[ich]  =						\
	ir->GetInteger(slotstring, ch_var_str, sp->_param[CHANNEL_COMMON]); \
    }

  _CHANNEL_SEARCH("RECORD_LENGTH", record_length);
  _CHANNEL_SEARCH("GAIN_FACTOR", gain_factor);
  _CHANNEL_SEARCH("MAX_TAIL", max_tail);
  _CHANNEL_SEARCH("PRE_TRIGGER", pre_trigger);
  _CHANNEL_SEARCH("N_LFW", n_lfw);

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

  _CHANNEL_SEARCH("BLINE_DEFVALUE", bline_defvalue);
  _CHANNEL_SEARCH("PULSE_POLARITY", pulse_polarity);
  _CHANNEL_SEARCH("TRG_THRESHOLD", trg_threshold);
  _CHANNEL_SEARCH("DC_OFFSET", dc_offset);



  // fill the defaults with ALLSLOTS
  if(slotstring.compare("ALLSLOTS") == 0)
    {
      memcpy(&defparam, &all_param, sizeof(caen1725param_t));
      for(int32_t ip = 0; ip < MAX_VME_SLOTS; ip++)
	{
	  memcpy(&param[ip], &defparam, sizeof(caen1725param_t));
	}
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

/**
 * @brief Write the local parameter structure for the specified slot to the library
 * @param[in] id slot id
 * @return 0
 */
int32_t
param2caen(int32_t id)
{
  /* Write the parameters to the device */
  { // hardcoded, atm
    uint32_t trg_in_mode = 0, // 0 : TRG-IN as common trigger
      veto_polarity = 1,      // 1 : Veto active on high logic level
      frag_trunc_event = 1;   // 1 : enabled

    c1725SetBoardConfiguration(id, trg_in_mode, veto_polarity, frag_trunc_event);
  }

#ifdef __notdoneyet
  c1725SetAcquisitionControl(int32_t id, uint32_t mode, uint32_t arm, uint32_t clocksource,
			     uint32_t lvds_busy_enable, uint32_t lvds_veto_enable,
			     uint32_t lvds_runin_enable);

#endif //__notdoneyet

  { // hardcoded, atm
    // Internal triggers
    uint32_t channel_enable = 0, majority_coincidence_window = 0, majority_level = 0;
    // External triggers
    uint32_t lvds_trigger_enable = 0, external_trigger_enable = 1, software_trigger_enable = 1;

    c1725SetGlobalTrigger(id, channel_enable, majority_coincidence_window, majority_level,
			  lvds_trigger_enable, external_trigger_enable,
			  software_trigger_enable);
  }

  { // hardcoded, atm
    // Internal trigger settings
    uint32_t channel_enable = 0, channel_logic = 0, majority_level = 0;
    // External triggers
    uint32_t lvds_trigger_enable = 0, external_trigger_enable = 1, software_trigger_enable = 1;

    c1725SetFPTrigOut(id, channel_enable, channel_logic,
		      majority_level, lvds_trigger_enable,
		      external_trigger_enable, software_trigger_enable);

    uint32_t lemo_enable = 1, lvds_mask = 0, trg_in_mask =0 , trg_out_mask = 0;
    c1725SetFPIO(id, param[id].fpio_level, lemo_enable,
		 lvds_mask, trg_in_mask, trg_out_mask);
  }


  c1725SetEnableChannelMask(id, param[id].enable_input_mask);

  { // hardcoded, atm

    uint32_t run_delay = 0, veto_delay = 0;
    c1725SetRunDelay(id, run_delay);
    c1725SetExtendedVetoDelay(id, veto_delay);


    int32_t dac = 0, mode = 0;
    c1725SetMonitorDAC(id, dac);
    c1725SetMonitorMode(id, mode);

    uint32_t intlevel = 0, optical_int = 0, vme_berr = 1, align64 = 1,
      address_relocate = 0, roak = 1, ext_blk_space = 0;

    c1725SetReadoutControl(id, intlevel, optical_int,
			   vme_berr, align64, address_relocate,
			   roak, ext_blk_space);
  }

  c1725SetMaxEventsPerBLT(id, param[id].max_events_per_blt);

  for(int32_t ichan = 0; ichan < C1725_MAX_ADC_CHANNELS; ichan++)
    {
      c1725SetRecordLength(id, ichan, param[id].record_length[ichan]);
      c1725SetDynamicRange(id, ichan, param[id].gain_factor[ichan]);
#ifdef NOTYETDEFINED
      c1725SetInputDelay(id, ichan, uint32_t delay);
#endif
      c1725SetPreTrigger(id, ichan, param[id].pre_trigger[ichan]);
      c1725SetTriggerThreshold(id, ichan, param[id].trg_threshold[ichan]);
      if(param[id].bline_defmode[ichan])
	c1725SetFixedBaseline(id, ichan, param[id].bline_defvalue[ichan]);

#ifdef NOTYETDEFINED
      c1725SetCoupleTriggerLogic(id, ichan, uint32_t logic);
      c1725SetSamplesUnderThreshold(id, ichan, uint32_t thres);
#endif
      c1725SetMaxmimumTail(id, ichan, param[id].max_tail[ichan]);

#ifdef NOTYETDEFINED
      c1725SetDPPControl(int32_t id, int32_t chan,
			 uint32_t test_pulse_enable, uint32_t test_pulse_rate,
			 uint32_t test_pulse_polarity, uint32_t self_trigger_enable);

      c1725SetCoupleOverTriggerLogic(id, ichan, uint32_t logic);
#endif

      c1725SetDCOffset(id, ichan, param[id].dc_offset[ichan]);
  }

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

  int32_t nc1725 = c1725N();
  for(int32_t ic = 0; ic < nc1725; ic++)
    {
      param2caen(c1725Slot(ic));
    }

  return 0;
}

// load in parameters to structure from filename
int32_t
caen1725Config(const char *filename)
{
  if(configDebug)
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
  if(configDebug)
    std::cout << __func__ << ": INFO: here" << std::endl;

  if(ir == NULL)
    return 1;

  if(configDebug)
    std::cout << "delete ir" << std::endl;
  delete ir;

  return 0;
}
