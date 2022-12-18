/*
 * File:
 *    c1725Status.c
 *
 * Description:
 *    Print the Status of C1725 to standard out
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

  int32_t stat;
  uint32_t address=0;

  if (argc > 1)
    {
      address = (uint32_t) strtoll(argv[1],NULL,16)&0xffffffff;
    }
  else
    {
      address = (5 << 19); // my test module
    }

  printf("\n %s: address = 0x%08x\n", argv[0], address);
  printf("----------------------------\n");

  stat = vmeOpenDefaultWindows();
  if(stat != OK)
    goto CLOSE;

  vmeCheckMutexHealth(1);
  vmeBusLock();

  c1725Init(address, 0, 1);
  c1725CheckAddresses();
  c1725GStatus(1);

 CLOSE:

  vmeBusUnlock();

  vmeClearException(1);

  stat = vmeCloseDefaultWindows();
  if (stat != OK)
    {
      printf("vmeCloseDefaultWindows failed: code 0x%08x\n",stat);
      return -1;
    }

  exit(0);
}
/*
  Local Variables:
  compile-command: "make -k c1725Status "
  End:
*/
