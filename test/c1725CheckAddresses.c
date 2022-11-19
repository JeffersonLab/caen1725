/*
 * File:
 *    c1725LibTest.c
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

int
main(int argc, char *argv[])
{

  printf("\nJLAB CAEN 1725 Library Tests\n");
  printf("----------------------------\n");

  c1725CheckAddresses();

  exit(0);
}
/*
  Local Variables:
  compile-command: "make -k c1725LibTest "
  End:
*/
