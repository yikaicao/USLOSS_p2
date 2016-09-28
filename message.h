
#define DEBUG2 1

/* ------------------------- Added Define ----------------------------------- */
#define RECEIVE_BLOCKED 11
#define SEND_BLOCKED    12
#define RELEASE_BLOCKED 13

#define clockHandlerMboxID 0
#define termHandlerMboxID 1

typedef struct mailSlot *slotPtr;
typedef struct mailbox   mailbox;
typedef struct mboxProc *mboxProcPtr;

struct mailbox {
    int       mboxID;
    // other items as needed...
    int          numSlots;
    int          slotSize;
    slotPtr     slotsList;
    mboxProcPtr blockedList;
};

struct mailSlot {
    int       mboxID;
    // other items as needed...
    int       slotID;
    int       msgSize;
    slotPtr  nextSlotPtr;
    char      msg[MAX_MESSAGE];
};

struct psrBits {
    unsigned int curMode:1;
    unsigned int curIntEnable:1;
    unsigned int prevMode:1;
    unsigned int prevIntEnable:1;
    unsigned int unused:28;
};

struct mboxProc {
    int         procID;
    int         status;
    mboxProcPtr nextBlocked;
    void        *buffer;
    int         bufferSize;
    int         recSize;
};

union psrValues {
    struct psrBits bits;
    unsigned int integerPart;
};

