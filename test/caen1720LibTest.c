/*
 * File:
 *    caen1720LibTest.c
 *
 * Description:
 *    Test the caen 1720 library
 *
 *
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "caen1720Lib.h"
#include "remexLib.h"

int 
main(int argc, char *argv[]) 
{

  int stat;
  extern int Nc1720;

  printf("\nJLAB CAEN 1720 Library Tests\n");
  printf("----------------------------\n");

  vmeOpenDefaultWindows();

  remexSetCmsgServer("dafarm28");
  remexInit(NULL,1);
  remexSetRedirect(1);

  if(c1720Init(0xa00000,0,1)!=OK)
    if(Nc1720==0)
      goto CLOSE;

  c1720PrintStatus(1);

  printf("<Enter> to continue\n");
  getchar();

 CLOSE:

  remexClose();
  vmeCloseDefaultWindows();

  exit(0);
}

