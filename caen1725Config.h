#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

  typedef struct
  {
    int32_t BOOL_IS_BETTER_FOR_YOU;
    char CHAR;
    float FLOAT;
    char hello[256];
    int32_t INT;
  } caen1725param_t;

  struct my_ini;
  typedef struct my_ini my_ini_t;

  my_ini_t *my_ini_create(const char* ini_filename);
  void my_ini_destroy(my_ini_t *m);
  void my_ini_do_stuff(my_ini_t *m, caen1725param_t *cp);

  /* routine prototypes */
  int32_t caen1725ConfigInitGlobals();
  int32_t caen1725Config(const char *filename);
  int32_t caen1725ConfigFree();

#ifdef __cplusplus
}
#endif
