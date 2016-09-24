/* ------------------------------------------------------------------------
   phase2.c
 
   University of Arizona
   Computer Science 452

   ------------------------------------------------------------------------ */

#include <phase1.h>
#include <phase2.h>
#include <usloss.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "message.h"

/* ------------------------- Prototypes ----------------------------------- */
int start1 (char *);
extern int start2 (char *);

/* ------------------------- Added Functions ----------------------------------- */
void enableInterrupts();
void disableInterrupts();
void check_kernel_mode(char *arg);
int check_io();

void emptyMailBox(int);
void emptyMailSlot(int);
void emptyMboxProc(int);

int getBoxID();
int getSlotID();
int getProcPos();

void printMailBoxTable();
void printBlockedList(int);
void printMboxProc(int);

void pushMailSlot(slotPtr *, slotPtr);
void dequeueSlotsList(slotPtr *);
void pushBlockedList(mboxProcPtr *, mboxProcPtr);
void dequeueBlockedList(mboxProcPtr *);

int numActive(int mboxID);

/* -------------------------- Globals ------------------------------------- */

int debugflag2 = 0;

// the mail boxes 
mailbox MailBoxTable[MAXMBOX];

// also need array of mail slots, array of function ptrs to system call 
// handlers, ...
/* -------------------------- Added Globals ------------------------------------- */
struct mailSlot MailSlotTable[MAXSLOTS];
struct mboxProc ProcTable[MAXPROC];

int slotCount = 0;

/* -------------------------- Functions ----------------------------------- */

/* ------------------------------------------------------------------------
   Name - start1
   Purpose - Initializes mailboxes and interrupt vector.
             Start the phase2 test process.
   Parameters - one, default arg passed by fork1, not used here.
   Returns - one to indicate normal quit.
   Side Effects - lots since it initializes the phase2 data structures.
   ----------------------------------------------------------------------- */
int start1(char *arg)
{
    int kid_pid, status;    // instance variables
    
    if (DEBUG2 && debugflag2)
        USLOSS_Console("start1(): at beginning\n");

    check_kernel_mode("start1");

    // Disable interrupts
    disableInterrupts();

    int i;
    // Initialize the mail box table, slots, & other data structures.
    for (i = 0; i < MAXMBOX; i++)
    {
        emptyMailBox(i);
    }
    for (i = 0; i < MAXSLOTS; i++)
    {
        emptyMailSlot(i);
    }
    for (i = 0; i < MAXPROC; i++)
    {
        emptyMboxProc(i);
    }
    // Initialize USLOSS_IntVec and system call handlers,
    // allocate mailboxes for interrupt handlers.  Etc...
    for (i = 0; i < 7; i++)
    {
        MboxCreate(0,MAX_MESSAGE);
    }

    enableInterrupts();

    // Create a process for start2, then block on a join until start2 quits
    if (DEBUG2 && debugflag2)
        USLOSS_Console("start1(): fork'ing start2 process\n");
    kid_pid = fork1("start2", start2, NULL, 4 * USLOSS_MIN_STACK, 1);
    if ( join(&status) != kid_pid ) {
        USLOSS_Console("start2(): join returned something other than ");
        USLOSS_Console("start2's pid\n");
    }

    return 0;
} /* start1 */


/* ------------------------------------------------------------------------
   Name - MboxCreate
   Purpose - gets a free mailbox from the table of mailboxes and initializes it 
   Parameters - maximum number of slots in the mailbox and the max size of a msg
                sent to the mailbox.
   Returns - -1 to indicate that no mailbox was created, or a value >= 0 as the
             mailbox id.
   Side Effects - initializes one element of the mail box array. 
   ----------------------------------------------------------------------- */
int MboxCreate(int numSlots, int slotSize)
{
    if (DEBUG2 && debugflag2)
        USLOSS_Console("MboxCreate(): entered\n");
    
    // slotSize or numSlots is incorrect
    if (slotSize < 0 || slotSize > MAX_MESSAGE || numSlots < 0 || numSlots > MAXSLOTS)
        return -1;
    
    // otherwise, a new Mbox may be created
    // attempt to an unused Mbox
    int newID = getBoxID();
    // no unused Mbox available
    if (newID == -1)
        return -1;
    
    // otherwise, space available, initialize it
    MailBoxTable[newID] = (struct mailbox)
    {
        .mboxID     = newID,
        .numSlots   = numSlots,
        .slotSize   = slotSize,
        .slotsList  = NULL,
        .blockedList= NULL
    };
    
    return newID;
} /* MboxCreate */


/* ------------------------------------------------------------------------
   Name - MboxSend
   Purpose - Put a message into a slot for the indicated mailbox.
             Block the sending process if no slot available.
   Parameters - mailbox id, pointer to data of msg, # of bytes in msg.
   Returns - zero if successful, -1 if invalid args.
   Side Effects - none.
   ----------------------------------------------------------------------- */
int MboxSend(int mboxID, void *msgPtr, int msgSize)
{
    /*
        1. no slot available, sender blocked
        2. slot available, receiver(s) blocked -> transfer msg -> unblock one receiver
        3. slot available, no receiver blocked, normal cases -> send msg
     */
    if (DEBUG2 && debugflag2)
        USLOSS_Console("MboxSend(): entered\n");
    
    // if mboxID not valid, or msgSize not valid
    if (mboxID >= MAXMBOX || mboxID < 0 ||
        MailBoxTable[mboxID].mboxID == -1 ||
        msgSize > MailBoxTable[mboxID].slotSize || msgSize < 0)
        return -1;
    // attempt to create a new slot
    int newSlotID = getSlotID();
    // no new slot available
    if (newSlotID == -1)
    {
        USLOSS_Console("MboxSend(): no new slot available, halting...\n");
        USLOSS_Halt(1);
    }
    
    
    // 1. no slot available, block sender
    if (numActive(mboxID) == MailBoxTable[mboxID].numSlots)
    {
        if (DEBUG2 && debugflag2)
            USLOSS_Console("MboxSend(): no slot available, blocking sender\n");
        // 1.1 update process table
        int newProcPos = getProcPos();
        if (newProcPos == -1)
        {
            USLOSS_Console("MboxSend(): no new proc slot available, halting...\n");
            USLOSS_Halt(1);
        }
        mboxProcPtr newBlocked = &ProcTable[newProcPos];
        newBlocked->procID      = getpid();
        newBlocked->status      = SEND_BLOCKED;
        newBlocked->nextBlocked = NULL;
        newBlocked->buffer      = msgPtr;
        newBlocked->bufferSize  = msgSize;
        newBlocked->recSize     = -1;
        // 1.2 update blockedList
        pushBlockedList(&MailBoxTable[mboxID].blockedList, newBlocked);
        
        //----------------------------------------//
        blockMe(SEND_BLOCKED);  // see MboxReceive Section 2 for steps
        //----------------------------------------//
        
        // 1.3 update process table again
        emptyMboxProc(newProcPos);
        
        return 0;
    }
    
    // 2. slot available (checked above), but at least one receiver blocked
    if (MailBoxTable[mboxID].blockedList != NULL)
    {
        if (DEBUG2 && debugflag2)
        {
            USLOSS_Console("MboxSend(): unblocking receiver(s)\n");
            printBlockedList(mboxID);
        }
        
        // 2.1 transfer msg
        if (MailBoxTable[mboxID].blockedList->bufferSize < msgSize)
            return -1;
        memcpy(MailBoxTable[mboxID].blockedList->buffer, msgPtr, msgSize);
        MailBoxTable[mboxID].blockedList->recSize = msgSize;
        // 2.2 dequeue blocked list
        int toBeUnblockedID = MailBoxTable[mboxID].blockedList->procID;
        dequeueBlockedList(&MailBoxTable[mboxID].blockedList);
        // 2.3 unblock
        unblockProc(toBeUnblockedID);
        return 0;
    }
    
    // 3. normal cases
    // setup new slot
    slotPtr newSlot     = &MailSlotTable[newSlotID];
    newSlot->mboxID     = mboxID;
    newSlot->slotID     = newSlotID;
    newSlot->msgSize    = msgSize;
    newSlot->nextSlotPtr= NULL;
    memset(newSlot->msg, 0, MAX_MESSAGE);
    memcpy(newSlot->msg, msgPtr, msgSize);
    // insert new slot
    pushMailSlot(&MailBoxTable[mboxID].slotsList, newSlot);
    
    return 0;
    
} /* MboxSend */


/* ------------------------------------------------------------------------
   Name - MboxReceive
   Purpose - Get a msg from a slot of the indicated mailbox.
             Block the receiving process if no msg available.
   Parameters - mailbox id, pointer to put data of msg, max # of bytes that
                can be received.
   Returns - actual size of msg if successful, -1 if invalid args.
   Side Effects - none.
   ----------------------------------------------------------------------- */
int MboxReceive(int mboxID, void *msgPtr, int msgReceiveSize)
{
    /*
        1. no msg, receiver blocked
        2. has msg, sender(s) blocked -> receive -> unblock one sender
        3. has msg, no sender blocked, normal cases
     */
    if (DEBUG2 && debugflag2)
        USLOSS_Console("MboxReceive(): entered\n");
    
    // if arguments not valid
    if (mboxID >= MAXMBOX || mboxID < 0 ||
        MailBoxTable[mboxID].mboxID == -1 ||
        msgReceiveSize < 0)
        return -1;

    // 1. no msg, receiver blocked
    if (MailBoxTable[mboxID].slotsList == NULL)
    {
        if (DEBUG2 && debugflag2)
            USLOSS_Console("MboxReceive(): no msg, blocking receiver\n");
        // 1.1 update process table
        int newProcPos = getProcPos();
        if (newProcPos == -1)
        {
            USLOSS_Console("MboxSend(): no new proc slot available, halting...\n");
            USLOSS_Halt(1);
        }
        mboxProcPtr newBlocked = &ProcTable[newProcPos];
        newBlocked->procID     = getpid();
        newBlocked->status     = RECEIVE_BLOCKED;
        newBlocked->nextBlocked= NULL;
        newBlocked->buffer     = msgPtr;
        newBlocked->bufferSize = msgReceiveSize;
        newBlocked->recSize    = -1;
        // 1.2 update blocked list
        pushBlockedList(&MailBoxTable[mboxID].blockedList, newBlocked);
        
        //----------------------------------------//
        blockMe(RECEIVE_BLOCKED);   // see MboxSend Section 2 for next steps
        //----------------------------------------//
        
        // 1.3 update process table again
        int recSize = ProcTable[newProcPos].recSize;
        emptyMboxProc(newProcPos);
        
        return recSize;
    }
    
    // 2. has msg (checked above), at least one sender blocked
    if (MailBoxTable[mboxID].blockedList != NULL)
    {
        if (DEBUG2 && debugflag2) {
            USLOSS_Console("MboxReceive(): has msg, unblocking sender(s)\n");
            printBlockedList(mboxID);
        }
        // 2.1 receive msg
        int recSize = MailBoxTable[mboxID].slotsList->msgSize;
        if(msgReceiveSize < recSize)
            return -1;
        memcpy(msgPtr, MailBoxTable[mboxID].slotsList->msg, recSize);
        // 2.2 dequeue slotsList
        dequeueSlotsList(&MailBoxTable[mboxID].slotsList);
        // 2.3 push (blocked) mail slot
        int newSlotID = getSlotID();
        slotPtr wasBlocked = &MailSlotTable[newSlotID];
        wasBlocked->mboxID      = mboxID;
        wasBlocked->slotID      = newSlotID;
        wasBlocked->msgSize     = MailBoxTable[mboxID].blockedList->bufferSize;
        wasBlocked->nextSlotPtr = NULL;
        memcpy(wasBlocked->msg, MailBoxTable[mboxID].blockedList->buffer,
               MailBoxTable[mboxID].blockedList->bufferSize);
        pushMailSlot(&MailBoxTable[mboxID].slotsList, wasBlocked);
        // 2.4 dequeue blockedList
        int toBeUnblockedID = MailBoxTable[mboxID].blockedList->procID;
        dequeueBlockedList(&MailBoxTable[mboxID].blockedList);
        // 2.5 unblockProc
        unblockProc(toBeUnblockedID);
        return recSize;
    }
    
    // 3. normal cases
    // return actual size of msg
    int toReturn;
    // receive msg
    if (MailBoxTable[mboxID].slotsList->msgSize > msgReceiveSize || MailBoxTable[mboxID].slotsList->msgSize < 0)
        return -1;
    toReturn = MailBoxTable[mboxID].slotsList->msgSize;
    memcpy(msgPtr, MailBoxTable[mboxID].slotsList->msg, MailBoxTable[mboxID].slotsList->msgSize);
    dequeueSlotsList(&MailBoxTable[mboxID].slotsList);
    
    return toReturn;
} /* MboxReceive */




/* %%%%%%%%%%%%%%%%%%%%%%%%% Added Functions %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%% */

/* ------------------------- enableInterrupts ----------------------------------- */
void enableInterrupts()
{
    // turn the interrupts on if we are in kernel mode
    if( (USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()) == 0 ) {
        //not in kernel mode
        USLOSS_Console("Kernel Error: Not in kernel mode, may not ");
        USLOSS_Console("disable interrupts\n");
        USLOSS_Halt(1);
    } else
        //We ARE in kernel mode
        USLOSS_PsrSet( USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT );
}

/* ------------------------- disableInterrupts ----------------------------------- */
void disableInterrupts()
{
    // turn the interrupts OFF iff we are in kernel mode
    if( (USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()) == 0 ) {
        //not in kernel mode
        USLOSS_Console("Kernel Error: Not in kernel mode, may not ");
        USLOSS_Console("disable interrupts\n");
        USLOSS_Halt(1);
    } else
        // We ARE in kernel mode
        USLOSS_PsrSet( USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_INT );
} /* disableInterrupts */

/* ------------------------- check_kernel_mode ----------------------------------- */
void check_kernel_mode(char *arg)
{
    if (!(USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()))
    {
        USLOSS_Console("%s(): called while in user mode. Halting...\n", arg);
    }
} /* check_kernel_mode */

/* ------------------------- check_io ----------------------------------- */
int check_io()
{
    return 0;
}

/* ------------------------- emptyMailBox ----------------------------------- */
void emptyMailBox(int id)
{
    MailBoxTable[id] = (struct mailbox)
    {
        .mboxID     = -1,
        .numSlots   = -1,
        .slotSize   = -1,
        .slotsList  = NULL,
        .blockedList= NULL
    };
}

/* ------------------------- emptyMailSlot ------------------------- */
void emptyMailSlot(int id)
{
    MailSlotTable[id] = (struct mailSlot)
    {
        .mboxID     = -1,
        .slotID     = -1,
        .msgSize    = -1,
        .nextSlotPtr= NULL,
    };
    memset(MailSlotTable[id].msg, 0, MAX_MESSAGE);
}

/* ------------------------- emptyMboxProc ------------------------- */
void emptyMboxProc(int id)
{
    ProcTable[id] = (struct mboxProc)
    {
        .procID     = -1,
        .status     = -1,
        .nextBlocked= NULL,
        .buffer     = NULL,
        .bufferSize = -1,
        .recSize    = -1
    };
}

/* ------------------------- getBoxID ----------------------------------- */
int getBoxID()
{
    for (int i = 0; i < MAXMBOX; i++)
        if (MailBoxTable[i].mboxID == -1)
            return i;
    return -1;
}

/* ------------------------- getSlotID ----------------------------------- */
int getSlotID()
{
    for (int i = 0; i < MAXSLOTS; i++)
        if (MailSlotTable[i].mboxID == -1)
            return i;
    return -1;
}

/* ------------------------- getProcPos ----------------------------------- */
int getProcPos()
{
    for (int i = 0; i < MAXPROC; i++)
        if (ProcTable[i].procID == -1)
            return i;
    return -1;
}

/* ------------------------- printMailBoxTable ------------------------- */
void printMailBoxTable()
{
    int i;
    for (i = 0; i < MAXMBOX; i++)
    {
        mailbox curMB = MailBoxTable[i];
        if (curMB.mboxID != -1)
            USLOSS_Console("MailBoxTable[%d] \tnumSlots = %d \tslotSize = %d\n", i, curMB.numSlots, curMB.slotSize);
    }
}

/* ------------------------- printBlockedList ------------------------- */
void printBlockedList(int id)
{
    mboxProcPtr tmp = MailBoxTable[id].blockedList;
    while(tmp != NULL)
    {
        USLOSS_Console("blocked pid = %d, status = %d, bufferSize = %d\n",
                       tmp->procID,
                       tmp->status,
                       tmp->bufferSize);
        tmp = tmp->nextBlocked;
    }
}

/* ------------------------- printMboxProc ------------------------- */
void printMboxProc(int id)
{
    mboxProcPtr tmp = &ProcTable[id];
    USLOSS_Console("procID = %d, status = %d, bufferSize = %d, recSize = %d\n",
                   tmp->procID,
                   tmp->status,
                   tmp->bufferSize,
                   tmp->recSize);
}

/* ------------------------- pushMailSlot -------------------------
 Side effect: update slotsList, increment slotCount
 */
void pushMailSlot(slotPtr *slotsList, slotPtr aMailSlot)
{
    // no slot in this MailBox, insert at the top
    if (*slotsList == NULL)
    {
        *slotsList = aMailSlot;
        
        // increment
        slotCount++;
        return;
    }
    // insert at the bottom
    else
    {
        slotPtr tmp = *slotsList;
        while(tmp->nextSlotPtr != NULL)
            tmp = tmp->nextSlotPtr;
        tmp->nextSlotPtr = aMailSlot;
        
        // increment
        slotCount++;
        return;
    }
}

/* ------------------------- dequeueSlotsList ------------------------- */
void dequeueSlotsList(slotPtr *slotsList)
{
    // why would I dequeue if there is no mail in the table?
    if (*slotsList == NULL)
        return;
    
    // delete mail slot both in MailSlotTable and MailBox
    slotPtr tmp = *slotsList;
    *slotsList = tmp->nextSlotPtr;
    emptyMailSlot(tmp->slotID);
    
    // decrement
    slotCount--;
}

/* ------------------------- pushBlockedList ------------------------- */
void pushBlockedList(mboxProcPtr *blockedList, mboxProcPtr newBlocked)
{
    // no blocked process yet
    if (*blockedList == NULL)
    {
        *blockedList = newBlocked;
    }
    // has other blocked process(es)
    else
    {
        mboxProcPtr aProc = *blockedList;
        while(aProc->nextBlocked != NULL)
            aProc = aProc->nextBlocked;
        aProc->nextBlocked = newBlocked;
    }
}

/* ------------------------- dequeueBlockedList ------------------------- */
void dequeueBlockedList(mboxProcPtr *blockedList)
{
    // why would I dequeue if there is no blocked process in the list?
    if (*blockedList == NULL)
        return;
    
    // delete process in blockedList
    mboxProcPtr tmp = *blockedList;
    *blockedList = tmp->nextBlocked;
}

/* ------------------------- numActive ------------------------- */
int numActive(int id)
{
    slotPtr tmp = MailBoxTable[id].slotsList;
    int toReturn = 0;
    while (tmp != NULL)
    {
        tmp = tmp->nextSlotPtr;
        toReturn++;
    }
    return toReturn;
}
