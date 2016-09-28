#include <stdio.h>
#include <phase1.h>
#include <phase2.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "message.h"
#include "handler.h"

extern int debugflag2;

static int fifth = 1;

/* an error method to handle invalid syscalls */
void nullsys(systemArgs *args)
{
    USLOSS_Console("nullsys(): Invalid syscall %d. Halting...\n", args->number);
    USLOSS_Halt(1);
} /* nullsys */


void clockHandler2(int dev, void *arg)
{
    int msg = 0;
    int *msgPtr = &msg;
    
    if (DEBUG2 && debugflag2)
        USLOSS_Console("clockHandler2(): called\n");
    
    if (fifth == 4)
    {
        timeSlice();
    }
    if (fifth == 5)
    {
        fifth = 0;
        MboxCondSend(clockHandlerMboxID, msgPtr, sizeof(int));
    }
    fifth++;

} /* clockHandler */


void diskHandler(int dev, void *arg)
{
    if (DEBUG2 && debugflag2)
        USLOSS_Console("diskHandler(): called\n");

} /* diskHandler */


void termHandler(int dev, void *arg)
{
    if (DEBUG2 && debugflag2)
        USLOSS_Console("termHandler(): called\n");
    
    long unit = (long) arg;
    
    if (unit < 0 || unit >= 4)
    {
        USLOSS_Console("termHandler(): invalid unit number, halting..\n");
        USLOSS_Halt(1);
    }
    int status = 0;
    USLOSS_DeviceInput(USLOSS_TERM_DEV, unit, &status);
    MboxCondSend(termHandlerMboxID + unit - 1, &status, sizeof(status));
    
} /* termHandler */

void syscallHandler(int dev, void *arg)
{
    if (DEBUG2 && debugflag2)
        USLOSS_Console("syscallHandler(): called\n");
    
    systemArgs *sysArgs = malloc(sizeof(systemArgs));
    sysArgs = arg;
    
    if (sysArgs->number < 0 || sysArgs->number >= MAXSYSCALLS)
    {
        USLOSS_Console("syscallHandler(): sys number %d is wrong.  Halting...\n", sysArgs->number);
        USLOSS_Halt(1);
    }

    nullsys(sysArgs);
} /* syscallHandler */
