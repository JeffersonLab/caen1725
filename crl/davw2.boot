
mRouteAdd("224.0.0.0","129.57.29.0",0xf0000000,0,0)

# Load host table
< /daqfs/home/abbottd/VXKERN/vxhosts.boot

# Setup environment to load coda_roc
putenv "MSQL_TCP_HOST=dafarm28"
putenv "EXPID=DAQDEVEL"
putenv "TCL_LIBRARY=/daqfs/coda/2.6.2/common/lib/tcl7.4"
putenv "ITCL_LIBRARY=/daqfs/coda/2.6.2/common/lib/itcl2.0"
putenv "DP_LIBRARY=/daqfs/coda/2.6.2/common/lib/dp"
putenv "SESSION=daqSession"


# Load Tempe DMA Library (for MV6100)
cd "/daqfs/mizar/home/abbottd/vxWorks/tempeDma"
ld < usrTempeDma.o
usrVmeDmaConfig(2,2,0)

# Load cMsg Stuff
cd "/daqfs/coda/2.6.2/cMsg/vxworks-ppc"
ld< lib/libcmsgRegex.o
ld< lib/libcmsg.o


cd "/daqfs/coda/2.6.2/VXWORKSPPC55/bin"
ld < coda_roc_rc3.6


# Spawn tasks
taskSpawn ("ROC",200,8,250000,coda_roc,"","-s","daqSession","-objects","roc35 ROC")





ld < /daqfs/home/moffit/work/v1495/miniTS/vx/miniTSlib.o
