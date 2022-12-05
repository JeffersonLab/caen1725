#include <iostream>
#include <string>
#include <sstream>
#include "caen1725Config.h"
#include "INIReader.h"
#include "caen1725Lib.h"

// place to store the ini INIReader instance
typedef struct caen1725Ini
{
  void *ir;
} caen1725Ini_t;

static caen1725Ini_t *cfg;
static caen1725param_t *param;

int32_t
caen1725ConfigInitGlobals()
{
  std::cout << __func__ << ": INFO: here" << std::endl;
  // create ini object
  cfg = (caen1725Ini_t *) malloc(sizeof(cfg));

  // initialize parameters in structure
  param = (caen1725param_t *) malloc(sizeof(param));

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


// load in parameters to structure from filename
int32_t
caen1725Config(const char *filename)
{
  std::cout << __func__ << ": INFO: here" << std::endl;

  INIReader *ir = new INIReader(filename);
  if(ir->ParseError() < 0)
    {
      std::cout << "Can't load: " << filename << std::endl;
      return 1;
    }

  cfg->ir = ir;

  std::cout << "Found sections : " << sections(*ir) << std::endl;

  return 0;
}

// destroy the ini object
int32_t
caen1725ConfigFree()
{
  std::cout << __func__ << ": INFO: here" << std::endl;

  if(cfg->ir == NULL)
    return 1;

  std::cout << "delete ir" << std::endl;
  delete (INIReader *)cfg->ir;

  std::cout << "free cfg" << std::endl;
  if(cfg)
    free(cfg);

  if(param == NULL)
    return 1;

  std::cout << "free param" << std::endl;
  free(param);

  return 0;
}
