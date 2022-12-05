/*
 * File:
 *    c1725ConfigTest.c
 *
 * Description:
 *    Test the caen 1725 library
 *
 *
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "caen1725Lib.h"
#include "caen1725Config.h"

int
main(int argc, char *argv[])
{

  printf("\nJLAB CAEN 1725 Library Tests\n");
  printf("----------------------------\n");

  caen1725ConfigInitGlobals();

  if(argc == 2)
    caen1725Config(argv[1]);

  caen1725ConfigFree();

  caen1725ConfigPrintParameters(0);
  caen1725ConfigPrintParameters(7);
  exit(0);
}
/*
  Local Variables:
  compile-command: "make -k c1725ConfigTest "
  End:
*/
