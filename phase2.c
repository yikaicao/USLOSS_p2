/* ------------------------------------------------------------------------
   phase2.c
 
   University of Arizona
   Computer Science 452

   ------------------------------------------------------------------------ */
//TODO enable interrupts

#include <phase1.h>
#include <phase2.h>
#include <usloss.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "message.h"
#include "handler.h"

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
    USLOSS_IntVec[USLOSS_CLOCK_INT] = clockHandler2;
    USLOSS_IntVec[USLOSS_DISK_INT] = diskHandler;
    USLOSS_IntVec[USLOSS_TERM_INT] = termHandler;
    USLOSS_IntVec[USLOSS_SYSCALL_INT] = syscallHandler;
    // allocate mailboxes for interrupt handlers.  Etc...
    for (i = 0; i < 7; i++)
    {
        MboxCreate(0, MAX_MESSAGE);
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
    
    check_kernel_mode("MboxCreate");
    // slotSize or numSlots is incorrect
    if (slotSize < 0 || slotSize > MAX_MESSAGE || numSlots < 0)
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
        0. 0-slot mailbox
        1. no slot available, sender blocked
        2. slot available, receiver(s) blocked -> transfer msg -> unblock one receiver
        3. slot available, no receiver blocked, normal cases -> send msg
     */
    if (DEBUG2 && debugflag2)
        USLOSS_Console("MboxSend(): entered\n");
    
    check_kernel_mode("MboxSend");
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
    
    /*  
        0. 0-slot mailbox
        0.1 there is at least one blocked process
        0.1.1 blocked on send -> enqueue
        0.1.2 blocked on receive -> transfer msg -> unblock one receiver
        0.2 there is no previously blocked process
        0.2.1 current gets blocked on send
     */
    // 0. 0-slot mailbox
    if (MailBoxTable[mboxID].numSlots == 0)
    {
        if (DEBUG2 && debugflag2)
            USLOSS_Console("MboxSend(): sending to a 0-slot mailbox\n");
        // 0.1 there is at least one blocked process
        if (MailBoxTable[mboxID].blockedList != NULL)
        {
            // 0.1.1 blocked on send
            if (MailBoxTable[mboxID].blockedList->status == SEND_BLOCKED)
            {
                if (DEBUG2 && debugflag2)
                    USLOSS_Console("MboxSend(): 0-slot blocked on send\n");
                // 0.1.1.1 update process table
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
                // 0.1.1.2 update blockedList
                pushBlockedList(&MailBoxTable[mboxID].blockedList, newBlocked);
                // 0.1.1.3 block invoking process
                blockMe(SEND_BLOCKED);
                // 0.1.1.4 update process table again
                emptyMboxProc(newProcPos);
                // 0.1.1.5 check if current mailbox is released
                if (MailBoxTable[mboxID].mboxID == -1)
                    return -3;
                // 0.1.1.6 check if invoking process is zapped
                if (isZapped())
                    return -3;
                return 0;
            }
            // 0.1.2 blocked on receive
            else if (MailBoxTable[mboxID].blockedList->status == RECEIVE_BLOCKED)
            {
                if (DEBUG2 && debugflag2)
                    USLOSS_Console("MboxSend(): 0-slot blocked on receive\n");
                // 0.1.2.1 transfer msg
                if (MailBoxTable[mboxID].blockedList->bufferSize < msgSize)
                {
                    int toBeUnblockedID = MailBoxTable[mboxID].blockedList->procID;
                    dequeueBlockedList(&MailBoxTable[mboxID].blockedList);
                    unblockProc(toBeUnblockedID);
                    return -1;
                }
                memcpy(MailBoxTable[mboxID].blockedList->buffer, msgPtr, msgSize);
                MailBoxTable[mboxID].blockedList->recSize = msgSize;
                // 0.1.2.2 dequeue blocked list
                int toBeUnblockedID = MailBoxTable[mboxID].blockedList->procID;
                dequeueBlockedList(&MailBoxTable[mboxID].blockedList);
                // 0.1.2.3 unblock
                unblockProc(toBeUnblockedID);
                return 0;
            }
            else
            {
                USLOSS_Console("MboxSend(): 0-slot blocked on unknown status, halting..\n");
                USLOSS_Halt(1);
            }
        }
        // 0.2 follows into case 1, so it is blank here
    }
    
    // 0.2 no slot available, block sender
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
        if (DEBUG2 && debugflag2)
            USLOSS_Console("process %d is unblocked in mbsend, current mboxID = %d\n",
                           getpid(), MailBoxTable[mboxID].mboxID);
        
        // 1.3 update process table again
        emptyMboxProc(newProcPos);
        
        // 1.4 check if current mailbox is released
        if (MailBoxTable[mboxID].mboxID == -1)
            return -3;
        
        // 1.5 check if this process is zapped
        if (isZapped())
            return -3;
        
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
        
        // 2.0 check if bufferSize is large enough to receive
        if (MailBoxTable[mboxID].blockedList->bufferSize < msgSize)
        {
            // 2.2 dequeue blocked list
            int toBeUnblockedID = MailBoxTable[mboxID].blockedList->procID;
            dequeueBlockedList(&MailBoxTable[mboxID].blockedList);
            // 2.3 unblock
            unblockProc(toBeUnblockedID);
            return -1;
        }
        // 2.1 transfer msg
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
int MboxReceive(int mboxID, void *msgPtr, int msgRecSize)
{
    /*
        0. 0-slot mailbox
        1. no msg, receiver blocked
        2. has msg, sender(s) blocked -> receive -> unblock one sender
        3. has msg, no sender blocked, normal cases
     */
    if (DEBUG2 && debugflag2)
        USLOSS_Console("MboxReceive(): entered\n");
    
    check_kernel_mode("MboxReceive");
    // if arguments not valid
    if (mboxID >= MAXMBOX || mboxID < 0 ||
        MailBoxTable[mboxID].mboxID == -1 ||
        msgRecSize < 0)
        return -1;

    
    /*
        0. 0-slot mailbox
        0.1 there is at least one blocked process
        0.1.1 blocked on send -> receive msg -> dequeue
        0.1.2 blocked on receive -> enqueue
        0.2 there is no previously blocked process -> block invoking process
     */
    // 0. 0-slot mailbox
    if (MailBoxTable[mboxID].numSlots == 0)
    {
        if (DEBUG2 && debugflag2)
            USLOSS_Console("MboxReceive(): receive from a 0-slot mailbox\n");
        // 0.1 there is at least one blocked process
        if (MailBoxTable[mboxID].blockedList != NULL)
        {
            // 0.1.1 blocked on send
            if (MailBoxTable[mboxID].blockedList->status == SEND_BLOCKED)
            {
                if (DEBUG2 && debugflag2)
                {
                    USLOSS_Console("MboxReceive(): 0-slot, unblocking sender(s)\n");
                    printBlockedList(mboxID);
                }
                // 0.1.1.1 receive msg
                int recSize = MailBoxTable[mboxID].blockedList->bufferSize;
                if (recSize > msgRecSize)
                {
                    int toBeUnblockedID = MailBoxTable[mboxID].blockedList->procID;
                    dequeueBlockedList(&MailBoxTable[mboxID].blockedList);
                    unblockProc(toBeUnblockedID);
                    return -1;
                }
                memcpy(msgPtr, MailBoxTable[mboxID].blockedList->buffer, recSize);
                // 0.1.1.2 dequeue blocked list
                int toBeUnblockedID = MailBoxTable[mboxID].blockedList->procID;
                dequeueBlockedList(&MailBoxTable[mboxID].blockedList);
                // 0.1.1.3 unblock proc
                unblockProc(toBeUnblockedID);
                return recSize;
            }
            // 0.1.2 blocked on receive
            else if (MailBoxTable[mboxID].blockedList->status == RECEIVE_BLOCKED)
            {
                // 0.1.2.1 update process table
                int newProcPos = getProcPos();
                if (newProcPos == -1)
                {
                    USLOSS_Console("MboxReceive(): no new proc slot available, halting...\n");
                    USLOSS_Halt(1);
                }
                mboxProcPtr newBlocked  = &ProcTable[newProcPos];
                newBlocked->procID      = getpid();
                newBlocked->status      = RECEIVE_BLOCKED;
                newBlocked->nextBlocked = NULL;
                newBlocked->buffer      = msgPtr;
                newBlocked->bufferSize  = msgRecSize;
                newBlocked->recSize     = -1;
                // 0.1.2.2 update blocked list
                pushBlockedList(&MailBoxTable[mboxID].blockedList, newBlocked);
                // 0.1.2.3 block invoking process
                blockMe(RECEIVE_BLOCKED);
                // 0.1.2.4 update process table again
                emptyMboxProc(newProcPos);
                // 0.1.2.5 check if current mailbox is released
                if (MailBoxTable[mboxID].mboxID == -1)
                    return -3;
                // 0.1.2.6 check if invoking process is zapped
                if (isZapped())
                    return -3;
                
                return 0;
            }
        }
        // 0.2 follows into case 1, so it is blank here
    }
    
    // 0.2 no msg, receiver blocked
    // 1. no msg, receiver blocked
    if (MailBoxTable[mboxID].slotsList == NULL)
    {
        if (DEBUG2 && debugflag2)
            USLOSS_Console("MboxReceive(): no msg, blocking receiver\n");
        // 1.1 update process table
        int newProcPos = getProcPos();
        if (newProcPos == -1)
        {
            USLOSS_Console("MboxReceive(): no new proc slot available, halting...\n");
            USLOSS_Halt(1);
        }
        mboxProcPtr newBlocked = &ProcTable[newProcPos];
        newBlocked->procID     = getpid();
        newBlocked->status     = RECEIVE_BLOCKED;
        newBlocked->nextBlocked= NULL;
        newBlocked->buffer     = msgPtr;
        newBlocked->bufferSize = msgRecSize;
        newBlocked->recSize    = -1;
        // 1.2 update blocked list
        pushBlockedList(&MailBoxTable[mboxID].blockedList, newBlocked);
        
        //----------------------------------------//
        blockMe(RECEIVE_BLOCKED);   // see MboxSend Section 2 for next steps
        //----------------------------------------//
        
        // 1.3 update process table again
        int recSize = ProcTable[newProcPos].recSize;
        emptyMboxProc(newProcPos);
        
        // 1.4 check if current mailbox is released
        if (MailBoxTable[mboxID].mboxID == -1)
            return -3;
        // 1.5 check if invoking process is zapped
        if (isZapped())
            return -3;
        
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
        if(msgRecSize < recSize)
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
    if (MailBoxTable[mboxID].slotsList->msgSize > msgRecSize || MailBoxTable[mboxID].slotsList->msgSize < 0)
        return -1;
    toReturn = MailBoxTable[mboxID].slotsList->msgSize;
    memcpy(msgPtr, MailBoxTable[mboxID].slotsList->msg, MailBoxTable[mboxID].slotsList->msgSize);
    dequeueSlotsList(&MailBoxTable[mboxID].slotsList);
    
    
    return toReturn;
} /* MboxReceive */

/* ------------------------------------------------------------------------
    Name - MboxRelease
    Purpose - Releases a previously created mailbox. Zap process(es) that 
    are blocked on the releasing mailbox.
    Parameters - mailbox id.
    Returns - zero if successful, -1 if mailBoxID is invalid, -3 if 
    process was zap’d while releasing the mailbox.
    Side Effects - The waiting process(es) will be zapped.
 ----------------------------------------------------------------------- */
int MboxRelease(int mboxID)
{
    if (DEBUG2 && debugflag2)
        USLOSS_Console("MboxRelease(): entered\n");
    
    check_kernel_mode("MboxRelease");
    disableInterrupts();
    if (mboxID >= MAXMBOX || mboxID < 0 ||
        MailBoxTable[mboxID].mboxID == -1)
        return -1;
    
    // inform waiting processes by makring its mxboID = -1, will completely empty it later
    MailBoxTable[mboxID].mboxID = -1;
    
    // unblock waiting processes on this mail box
    mboxProcPtr tmp = MailBoxTable[mboxID].blockedList;
    while (tmp != NULL)
    {
        int toBeReleasedProcID = tmp->procID;
        dequeueBlockedList(&MailBoxTable[mboxID].blockedList);
        unblockProc(toBeReleasedProcID);
        
        // update tmp
        tmp = MailBoxTable[mboxID].blockedList;
    }
    
    // actually wipe out the mail box
    if (DEBUG2 && debugflag2)
        USLOSS_Console("wiping out mail box..\n");
    emptyMailBox(mboxID);
    
    if (isZapped())
        return -3;
    
    enableInterrupts();
    return 0;
} /* MboxRelease */

/* ------------------------------------------------------------------------
    Name - MboxCondSend
    Purpose - Conditionaly send a message to mailbox. 
            Do not block the invoking process.
    Parameters - mailbox id, pointer to data of msg, # of bytes in msg.
    Returns - zero if successful, -1 if invalid args,
                -2 if mailbox full, message not sent; or no slots available
                -3 process was zap'd
    Side Effects - none.
 ----------------------------------------------------------------------- */
int MboxCondSend(int mboxID, void *msgPtr, int msgSize)
{
    /*
        0. 0-slot
        1. no slot available -> return -2
        2. slot available, receiver(s) blocked -> transfer msg -> unblock one receiver
        3. slot available, no receiver blocked, normal cases -> send msg
     */
    if (DEBUG2 && debugflag2)
        USLOSS_Console("MboxCondSend(): entered\n");
    
    check_kernel_mode("MboxCondSend");
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
        if (MailBoxTable[mboxID].numSlots == 0)
            return 0;
        return -2;
    }
    
    
    // 0. 0-slot
    if (MailBoxTable[mboxID].numSlots == 0)
    {
        // 0.1 at least one blocked process
        if (MailBoxTable[mboxID].blockedList != NULL)
        {
            // 0.1.1 blocked on send
            if (MailBoxTable[mboxID].blockedList->status == SEND_BLOCKED)
            {
                if (DEBUG2 && debugflag2)
                    USLOSS_Console("MboxCondSend(): 0-slot blocked on send\n");
                // 0.1.1.1 return 0 since conditional send
                return 0;
            }
            // 0.1.2 blocked on receive
            else if (MailBoxTable[mboxID].blockedList->status == RECEIVE_BLOCKED)
            {
                if (DEBUG2 && debugflag2)
                    USLOSS_Console("MboxCondSend(): 0-slot blocked on receive\n");
                // 0.1.2.1 transfer msg
                if (MailBoxTable[mboxID].blockedList->bufferSize < msgSize)
                {
                    int toBeUnblockedID = MailBoxTable[mboxID].blockedList->procID;
                    dequeueBlockedList(&MailBoxTable[mboxID].blockedList);
                    unblockProc(toBeUnblockedID);
                    return -1;
                }
                memcpy(MailBoxTable[mboxID].blockedList->buffer, msgPtr, msgSize);
                MailBoxTable[mboxID].blockedList->recSize = msgSize;
                // 0.1.2.2 dequeue blocked list
                int toBeUnblockedID = MailBoxTable[mboxID].blockedList->procID;
                dequeueBlockedList(&MailBoxTable[mboxID].blockedList);
                // 0.1.2.3 unblock
                unblockProc(toBeUnblockedID);
                return 0;
            }
        }
        // 0.2 no previously blocked
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
    }
    
    // 1. no slot available
    if (numActive(mboxID) == MailBoxTable[mboxID].numSlots)
        return -2;
    
    // 2. slot available (checked above), but at least one receiver blocked
    if (MailBoxTable[mboxID].blockedList != NULL)
    {
        if (DEBUG2 && debugflag2)
        {
            USLOSS_Console("MboxCondSend(): unblocking receiver(s)\n");
            printBlockedList(mboxID);
        }
        
        // 2.1 transfer msg
        if (MailBoxTable[mboxID].blockedList->bufferSize < msgSize)
        {
            int toBeUnblockedID = MailBoxTable[mboxID].blockedList->procID;
            dequeueBlockedList(&MailBoxTable[mboxID].blockedList);
            unblockProc(toBeUnblockedID);
            return -1;
        }
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
}

/* ------------------------------------------------------------------------
    Name - MboxCondReceive
    Purpose - Conditionaly receive a message to mailbox.
            Do not block the invoking process.
    Parameters - mailbox id, pointer to data of msg, # of bytes in msg.
    Returns - >= 0 the size of the message received,
            -1 if invalid args,
            -2 if mailbox empty
            -3 process was zap'd
    Side Effects - none.
 ----------------------------------------------------------------------- */
int MboxCondReceive(int mboxID, void *msgPtr, int msgRecSize)
{
    /*
        1. no msg, return -2
        2. has msg, sender(s) blocked -> receive -> unblock one sender
        3. has msg, no sender blocked, normal cases
     */
    if (DEBUG2 && debugflag2)
        USLOSS_Console("MboxCondReceive(): entered\n");
    
    check_kernel_mode("MboxCondReceive");
    if (mboxID >= MAXMBOX || mboxID < 0 ||
        MailBoxTable[mboxID].mboxID == -1 ||
        msgRecSize < 0)
        return -1;
    
    // 1. no msg
    if (numActive(mboxID) <= 0)
        return -2;
    
    // 2. has msg, sender(s) blocked
    if (MailBoxTable[mboxID].blockedList != NULL)
    {
        if (DEBUG2 && debugflag2)
        {
            USLOSS_Console("MboxCondReceive(): unblocking sender(s)\n");
            printBlockedList(mboxID);
        }
        // 2.1 receive msg
        int recSize = MailBoxTable[mboxID].slotsList->msgSize;
        if (DEBUG2 && debugflag2)
        {
            USLOSS_Console("MboxCondReceive(): unblocking sender(s)\n");
            printBlockedList(mboxID);
        }
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
        // 2.5 unblock proc
        unblockProc(toBeUnblockedID);
        return recSize;
    }
    
    // 3. normal cases
    // return actual size of msg
    int toReturn;
    // receive msg
    if (MailBoxTable[mboxID].slotsList->msgSize > msgRecSize || MailBoxTable[mboxID].slotsList->msgSize < 0)
        return -1;
    toReturn = MailBoxTable[mboxID].slotsList->msgSize;
    memcpy(msgPtr, MailBoxTable[mboxID].slotsList->msg, MailBoxTable[mboxID].slotsList->msgSize);
    dequeueSlotsList(&MailBoxTable[mboxID].slotsList);
    
    return toReturn;
}


/* ------------------------------------------------------------------------
    Name - waitDevice
    Purpose - Do a receive operation on the mailbox associated with the given unit of the device type.
    Parameters - type of device, unit # of device, status of relative device
    Returns - -1 if zap'd
               0 otherwise
    Side Effects - status register of given unit will be updated
 ----------------------------------------------------------------------- */
int waitDevice(int type, int unit, int *status)
{
    if (DEBUG2 && debugflag2)
        USLOSS_Console("waitDevice(): entered\n");
    
    switch(type)
    {
        case (USLOSS_CLOCK_INT):
            MboxReceive(clockHandlerMboxID, status, sizeof(long));
            return 0;
        case (USLOSS_TERM_INT):
            MboxReceive(termHandlerMboxID + unit - 1, status, sizeof(long));
            return 0;
        default:
            USLOSS_Console("waitDevice(): unknown device type, halting..\n");
            USLOSS_Halt(1);
    }
    
    return 0;
}

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
    int i;
    for (i = 0; i < MAXPROC; i++)
    {
        if (ProcTable[i].status > 10)
        {
            return 1;
        }
    }
        
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
            USLOSS_Console("\tMailBoxTable[%d] \tnumSlots = %d \tslotSize = %d\n", i, curMB.numSlots, curMB.slotSize);
    }
}

/* ------------------------- printBlockedList ------------------------- */
void printBlockedList(int id)
{
    mboxProcPtr tmp = MailBoxTable[id].blockedList;
    while(tmp != NULL)
    {
        USLOSS_Console("\tblocked pid = %d, status = %d, bufferSize = %d\n",
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
