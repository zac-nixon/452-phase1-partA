/*
 452 Phase 2 Part B
 Authors:
 Matt Seall
 Zachary Nixon
 */
#include <stdlib.h>
#include <phase1.h>
#include <phase2.h>
#include "usyscall.h"
#include <stdio.h>
#include <string.h>
#include <libuser.h>

#define US_IN_S 1000000

/* Structs */

typedef struct userArgStruct {
	int (*func)(void*);
	void *arg;
} userArgStruct;

typedef struct mailboxMessage{
	void *buf;
	int bufSize;
	struct mailboxMessage *next;
} message;

typedef struct clockDriverStruct{
	int inUse;
	P1_Semaphore sem;
	long startTime;
	long waitingTime;
} clockDriverStruct;

typedef struct mailbox{
	int id;
	int inUse;
	int maxSlots;
	int activeSlots;
	int maximumMessageSize;
	P1_Semaphore mutex;
	P1_Semaphore fulls;
	P1_Semaphore empties;
	message *queue;
} mailbox;

typedef struct usem{
	int id;
	P1_Semaphore sem;
	int inUse;	
} usem;

/*Prototypes*/
static int ClockDriver(void *arg);
static int TermDriver(void *arg);
static int DiskDriver(void *arg);
static int launch(void *arg);
static void queueInsert(void *m,int size,message **head);
static message * queuePop(message **head);
void sysHandler(int type, void *arg);
static int permissionCheck(void);
static void interruptsOn(void);
static void userMode(void);
void wakeUpProcesses(void);
int semAdd(P1_Semaphore s);
int semDelete(int id);
int validSem(int id);

/*All mailboxes*/
mailbox mailboxes[P2_MAX_MBOX];

/*The list of processes waiting on the clock driver struct*/
clockDriverStruct clockList[P1_MAXPROC];

/*Guard mailbox list*/
P1_Semaphore mBoxSemaphore;

/*Guard clock list*/
P1_Semaphore clockListSemaphore;

usem userSemList[P1_MAXSEM];
P1_Semaphore semGuard;

/*Semaphore and PIDS for Disk units*/
int diskPids[USLOSS_DISK_UNITS] = {0};
P1_Semaphore diskSem[USLOSS_DISK_UNITS] = {0};

/*Term Driver PIDS*/
int termPids[USLOSS_TERM_UNITS] = {0};

/*Term mailbox ids*/
int termMB[USLOSS_TERM_UNITS] = {0};

/*Term semaphores - make sure only 1 term is being written / wrote at a time*/
P1_Semaphore termSem[USLOSS_TERM_UNITS];

P1_Semaphore running;

int done = 0;
int P2_Startup(void *arg) {
	USLOSS_IntVec[USLOSS_SYSCALL_INT] = sysHandler;
	/*Init all mailboxes*/
	int i;
	for(i = 0; i < P2_MAX_MBOX;i++){
		mailboxes[i].id = i;
		mailboxes[i].inUse = 0;
		mailboxes[i].queue = NULL;
		mailboxes[i].activeSlots = 0;
	}

	for(i = 0; i < P1_MAXPROC;i++){
		clockList[i].inUse = 0;
	}
	
	for(i = 0; i < P1_MAXSEM;i++){
		userSemList[i].id = i;
		userSemList[i].inUse = 0;
	}

	mBoxSemaphore = P1_SemCreate(1);
	clockListSemaphore = P1_SemCreate(1);
	semGuard = P1_SemCreate(1);

	running = P1_SemCreate(0);
	int status;
	int pid;
	int clockPID;

	/*
	 * Create clock device driver
	 */
	clockPID = P1_Fork("Clock driver", ClockDriver, (void *) running,
	USLOSS_MIN_STACK, 2);
	if (clockPID == -1) {
		USLOSS_Console("Can't create clock driver\n");
	}
	/*
	 * Wait for the clock driver to start.
	 */
	P1_P(running);
        for(i = 0; i < USLOSS_TERM_UNITS;i++){
               	termPids[i] = P1_Fork("Term driver", TermDriver, (void *) i,USLOSS_MIN_STACK, 2);
                int termMB[USLOSS_TERM_UNITS] = {0};if (termPids[i] == -1) {
                        USLOSS_Console("Can't create term driver. Unit = %d\n",i);
                }
		P1_P(running);
        }
	/*
	Create Disk Driver
	*/
	for (i = 0; i < USLOSS_DISK_UNITS;i++) {
		diskSem[i] = P1_SemCreate(1);
		diskPids[i] = P1_Fork("Disk driver", DiskDriver, (void *) i, USLOSS_MIN_STACK,2);
	}
	// Only two disk units, so fork processes for each unit, 0 & 1
	// ...
	/*
	 * Create initial user-level process. You'll need to check error codes, etc. P2_Spawn
	 * and P2_Wait are assumed to be the kernel-level functions that implement the Spawn and
	 * Wait system calls, respectively (you can't invoke a system call from the kernel).
	 */
	pid = P2_Spawn("P3_Startup", P3_Startup, NULL, 4 * USLOSS_MIN_STACK, 3);
	pid = P2_Wait(&status);
	/*
	 * Kill the device drivers
	 */
	int a = P1_Kill(clockPID);
	printf("%d\n",a);
	// ...

	/*
	 * Join with the device drivers.
	 */
	// ...
	return 0;
}

void sysHandler(int type,void *arg) {
	if(arg == NULL){
		USLOSS_Console("USLOSS_Sysargs is NULL!\n");
		return;
	}
	USLOSS_Sysargs *sysArgs = (USLOSS_Sysargs *) arg;
	int retVal = 0;
	int retVal2 = 0;
	int *sector = 0;
	int *track = 0;
	int *disk = 0;
	interruptsOn();
	switch (sysArgs->number) {
	case SYS_TERMREAD: 
		break;
	case SYS_TERMWRITE:
		break;
	case SYS_SPAWN: //Part 1
		retVal = P2_Spawn(sysArgs->arg5, sysArgs->arg1, sysArgs->arg2,
				(int) sysArgs->arg3, (int) sysArgs->arg4);
		if (retVal == -3 || retVal == -2) {
			sysArgs->arg4 = (void *) -1;
			sysArgs->arg1 = (void *) -1;
		} else {
			sysArgs->arg1 = (void *) retVal;
			sysArgs->arg4 = (void *) 0;
		}
		break;
	case SYS_WAIT: //Part 1
		retVal = P2_Wait(&retVal2);
		if(retVal == -1){
			sysArgs->arg1 = (void *)-1;
			sysArgs->arg4 = (void *)-1;
			sysArgs->arg2 = (void *)-1;
		}else{
			sysArgs->arg1 = (void *)retVal;
			sysArgs->arg2 = (void *)retVal2;
			sysArgs->arg4 = (void *)0;
		}
		break;
	case SYS_TERMINATE: //Part 1
		P2_Terminate((int)sysArgs->arg1);
		break;
	case SYS_SLEEP: // Part 1
		retVal = P2_Sleep((int)sysArgs->arg1);
		sysArgs->arg4 = (void *) retVal;
		break;
	case SYS_DISKREAD:
		retVal = P2_DiskRead((int)sysArgs->arg5,(int)sysArgs->arg3,(int) sysArgs->arg4,(int)sysArgs->arg2,(void *)sysArgs->arg1);
                if (retVal == -1) {
                        sysArgs->arg4 = (void *)-1;
                        sysArgs->arg1 = (void *)-1;
                }
                else if (retVal == 0) {
                        sysArgs->arg1 = (void *)0;
                        sysArgs->arg4 = (void *)0;
                }else {
                        sysArgs->arg1 = (void *)retVal; //output is the disk's status register
                        sysArgs->arg4 = (void *)0;
                }
		break;
	case SYS_DISKWRITE:
		retVal = P2_DiskWrite((int)sysArgs->arg5,(int)sysArgs->arg3,(int) sysArgs->arg4,(int)sysArgs->arg2,(void *)sysArgs->arg1);
		if (retVal == -1) {
			sysArgs->arg4 = (void *)-1;
			sysArgs->arg1 = (void *)-1;
		}
		else if (retVal == 0) {
			sysArgs->arg1 = (void *)0;
			sysArgs->arg4 = (void *)0;
		}else {
			sysArgs->arg1 = (void *)retVal; //output is the disk's status register
			sysArgs->arg4 = (void *)0;
		}
		break;
	case SYS_DISKSIZE:
		retVal = P2_DiskSize((int)sysArgs->arg1,(int *)sector,(int *)track,(int *)disk);
		if (retVal == -1) {
			sysArgs->arg4 = (void *)-1;
		}else {
			sysArgs->arg1 = (void *)sector;
			sysArgs->arg2 = (void *)track;
			sysArgs->arg3 = (void *)disk;
			sysArgs->arg4 = (void *)0;
		}
		break;
	case SYS_GETTIMEOFDAY: //Part 1
		sysArgs->arg1 = (void *)USLOSS_Clock();
		break;
	case SYS_CPUTIME: //Part 1
		sysArgs->arg1 = (void *)P1_ReadTime();
		break;
	case SYS_DUMPPROCESSES: //Part 1
		P1_DumpProcesses();
		break;
	case SYS_GETPID: //Part 1
		sysArgs->arg1 = (void *) P1_GetPID();
		break;
	case SYS_SEMCREATE: //Part 1
		retVal = (int)sysArgs->arg1;
		if(retVal < 0){
			sysArgs->arg4 = (void *)-1;
		}else{
			sysArgs->arg4 = (void *)0;
			sysArgs->arg1 = (void *) semAdd(P1_SemCreate(retVal));
		}
		break;
	case SYS_SEMP: // Part 1
		if(validSem((int)sysArgs->arg1) == 0){
			sysArgs->arg4 = (void *) -1;
			return;
		}
		retVal = P1_P(userSemList[(int)sysArgs->arg1].sem);
		if(retVal < 0){
			sysArgs->arg4 = (void *) -1;
		}
		else{
			sysArgs->arg4 = (void *) 0;
		}
		break;
	case SYS_SEMV: // Part 1
                if(validSem((int)sysArgs->arg1) == 0){
                        sysArgs->arg4 = (void *) -1;
                        return;
                }
		retVal = P1_V(userSemList[(int)sysArgs->arg1].sem);
		if(retVal < 0){
			sysArgs->arg4 = (void *) -1;
		}
		else{
			sysArgs->arg4 = (void *) 0;
		}
		break;
	case SYS_SEMFREE: // Part 1
                if(validSem((int)sysArgs->arg1) == 0){
                        sysArgs->arg4 = (void *) -1;
                        return;
                }
		
		retVal = semDelete((int)sysArgs->arg1);
		if(retVal < 0){
			sysArgs->arg4 = (void *) -1;
		}
		else{
			sysArgs->arg4 = (void *) 0;
		}
		break;
	case SYS_MBOXCREATE: //Part 1
		retVal = P2_MboxCreate((int)sysArgs->arg1,(int)sysArgs->arg2);
		if(retVal == -2){
			sysArgs->arg1 = (void *) -1;
			sysArgs->arg4 = (void *) -1;
		}else{
			sysArgs->arg1 = (void *) retVal;
			sysArgs->arg4 = (void *) 0;
		}
		break;
	case SYS_MBOXRELEASE: // Part 1
		retVal = P2_MboxRelease((int)sysArgs->arg1);
		if(retVal < 0){
			sysArgs->arg4 = (void *) -1;
		}else{
			sysArgs->arg4 = (void *) 0;
		}
		break;
	case SYS_MBOXSEND: // Part 1
		retVal = P2_MboxSend((int)sysArgs->arg1,sysArgs->arg2,(int *)&sysArgs->arg3);
		if(retVal < 0){
			sysArgs->arg4 = (void *) -1;
		}else{
			sysArgs->arg4 = (void *) 0;
		}
		break;
	case SYS_MBOXRECEIVE: // Part 1
                retVal = P2_MboxReceive((int)sysArgs->arg1,sysArgs->arg2,(int *)&sysArgs->arg3);
                if(retVal < 0){
                        sysArgs->arg4 = (void *) -1;
                }else{
                        sysArgs->arg4 = (void *) 0;
			sysArgs->arg2 = (void *) retVal;
                }
		break;
	case SYS_MBOXCONDSEND: // Part 1
                retVal = P2_MboxCondSend((int)sysArgs->arg1,sysArgs->arg2,(int *)&sysArgs->arg3);
                if(retVal == -1){
                        sysArgs->arg4 = (void *) -1;
                }else if(retVal == -2){
			sysArgs->arg4 = (void *) 1;
		}
		else{
                        sysArgs->arg4 = (void *) 0;
                }
		break;
	case SYS_MBOXCONDRECEIVE: // Part 1
                retVal = P2_MboxCondReceive((int)sysArgs->arg1,sysArgs->arg2,(int *)&sysArgs->arg3);
                if(retVal == -1){
                        sysArgs->arg4 = (void *) -1;
                }else if(retVal == -2){
                        sysArgs->arg4 = (void *) 1;
                }
                else{
                        sysArgs->arg4 = (void *) 0;
			sysArgs->arg2 = (void *) retVal;
                }
		break;
	default:
		P1_Quit(1);
	}
}

int P2_Spawn(char *name, int (*func)(void*), void *arg, int stackSize,
		int priority) {
	if (permissionCheck()) {
		return -1;
	}
	userArgStruct* uas = malloc(sizeof(userArgStruct));
	uas->arg = arg;
	uas->func = func;
	int retVal = P1_Fork(name, launch, uas, stackSize, priority);
	return retVal;
}

int launch(void *arg) {
	userArgStruct *uas = (userArgStruct *) arg;
	interruptsOn();
	userMode();
	int rc = uas->func(uas->arg);
	free(uas);
	Sys_Terminate(rc);
	/*Never gets here*/
	return rc;
}

void P2_Terminate(int status) {
	if (permissionCheck()) {
		return;
	}
	P1_Quit(status);
}

int P2_Wait(int *status){
        if (permissionCheck()) {
                return -1;
        }
	return P1_Join(status);
}

int P2_Sleep(int seconds){
	if(permissionCheck() || seconds < 0){
		return -1;
	}
	P1_Semaphore s = P1_SemCreate(0);
	P1_P(clockListSemaphore);
	int i;
	for(i = 0; i < P1_MAXPROC;i++){
		if(clockList[i].inUse == 0){
			clockList[i].inUse = 1;
			clockList[i].sem = s;
			clockList[i].startTime = USLOSS_Clock();
			clockList[i].waitingTime = seconds * US_IN_S;
			break;
		}
	}
	P1_V(clockListSemaphore);
	P1_P(s);
	P1_SemFree(s);
	return 0;
}

int P2_MboxCreate(int slots, int size){
	if(permissionCheck()){
		return -1;
	}
	P1_P(mBoxSemaphore);
	if(slots <= 0 || size < 0){
		P1_V(mBoxSemaphore);
		return -2;
	}
	int i;
	for(i = 0; i < P2_MAX_MBOX;i++){
		if(mailboxes[i].inUse == 0){
			mailboxes[i].inUse = 1;
			mailboxes[i].maxSlots = slots;
			mailboxes[i].maximumMessageSize = size;
			mailboxes[i].mutex = P1_SemCreate(1);
			mailboxes[i].empties = P1_SemCreate(slots);
			mailboxes[i].fulls = P1_SemCreate(0);
			mailboxes[i].queue = NULL;
			mailboxes[i].activeSlots = 0;
			P1_V(mBoxSemaphore);
			return i;
		}
	}
	P1_V(mBoxSemaphore);
	return -1;
}

int P2_MboxRelease(int mbox){
	if(permissionCheck()){
		return -1;
	}
	P1_P(mBoxSemaphore);
	int i;
	for(i = 0; i < P2_MAX_MBOX;i++){
		if(mailboxes[i].inUse && mailboxes[i].id == mbox){
			if(P1_SemFree(mailboxes[i].mutex) != 0){
				USLOSS_Console("Processes waiting on mailbox. Halting.\n");
				USLOSS_Halt(1);
			}
			if(P1_SemFree(mailboxes[i].fulls) != 0){
				USLOSS_Console("Processes waiting on mailbox. Halting.\n");
				USLOSS_Halt(1);
			}
			if(P1_SemFree(mailboxes[i].empties) != 0){
				USLOSS_Console("Processes waiting on mailbox. Halting.\n");
				USLOSS_Halt(1);
			}

			while(mailboxes[i].queue != NULL){
				message *m = queuePop(&(mailboxes[i].queue));
				free(m->buf);
				free(m);
			}
			mailboxes[i].inUse = 0;
			P1_V(mBoxSemaphore);
			return 0;
		}
	}
	P1_V(mBoxSemaphore);
	return -1;
}

int P2_MboxSend(int mbox, void *msg, int *size){
	if(mbox < 0 || mbox >= P2_MAX_MBOX || mailboxes[mbox].inUse != 1 || mailboxes[mbox].maximumMessageSize < *size || permissionCheck()){
		return -1;
	}

	mailbox *cur = &(mailboxes[mbox]);
	P1_P(cur->empties);
	P1_P(cur->mutex);
	void *m = malloc(*size);
	memcpy(m,msg,*size);
	queueInsert(m,*size,&(cur->queue));
	cur->activeSlots++;
	P1_V(cur->mutex);
	P1_V(cur->fulls);
	return 0;
}

int P2_MboxCondSend(int mbox, void *msg, int *size){
	if(mbox < 0 || mbox >= P2_MAX_MBOX || mailboxes[mbox].inUse != 1 || mailboxes[mbox].maximumMessageSize < *size || permissionCheck()){
		return -1;
	}

	mailbox *cur = &(mailboxes[mbox]);
	P1_P(cur->mutex);
	if(cur->activeSlots < cur->maxSlots){
		P1_P(cur->empties); //Never blocks.
		void *m = malloc(*size);
		memcpy(m,msg,*size);
		queueInsert(m,*size,&(cur->queue));
		cur->activeSlots++;
		P1_V(cur->mutex);
		P1_V(cur->fulls);
		return 0;
	}
	P1_V(cur->mutex);
	return -2;
}

int P2_MboxReceive(int mbox, void *msg, int *size){
	if(mbox < 0 || mbox >= P2_MAX_MBOX || mailboxes[mbox].inUse != 1 || permissionCheck()){
		return -1;
	}
	mailbox *cur = &(mailboxes[mbox]);
	P1_P(cur->fulls);
	P1_P(cur->mutex);
	message *m = queuePop(&(cur->queue));
	int retVal = 0;
	if(*size > m->bufSize){
		retVal = m->bufSize;
	}else{
		retVal = *size;
	}
	memcpy(msg,m->buf,retVal);
	free(m->buf);
	free(m);
	cur->activeSlots--;
	P1_V(cur->mutex);
	P1_V(cur->empties);
	return retVal;
}

/*
 * 0 >= bytes write
 * -1 = invalid args
 * -2 = no message
 */
int P2_MboxCondReceive(int mbox, void *msg, int *size){
	if(mbox < 0 || mbox >= P2_MAX_MBOX || mailboxes[mbox].inUse != 1 || permissionCheck()){
		return -1;
	}
	mailbox *cur = &(mailboxes[mbox]);
	P1_P(cur->mutex);
	if(cur->activeSlots != 0){
		P1_P(cur->fulls); //Never blocks
		message *m = queuePop(&(cur->queue));
		int retVal = 0;
		if(*size > m->bufSize){
			retVal = m->bufSize;
		}else{
			retVal = *size;
		}
		memcpy(msg,m->buf,retVal);
		free(m->buf);
		free(m);
		cur->activeSlots--;
		P1_V(cur->mutex);
		P1_V(cur->empties);
		return retVal;
	}
	P1_V(cur->mutex);
	return -2;
}

static int ClockDriver(void *arg) {
	interruptsOn();
	int result;
	int status;
	int rc = 0;
	/*
	 * Let the parent know we are running and enable interrupts.
	 */
	P1_V(running);
	while (1) {
		result = P1_WaitDevice(USLOSS_CLOCK_DEV, 0, &status);
		if (result != 0) {
			rc = 1;
			goto done;
		}
		/*
		 * Compute the current time and wake up any processes
		 * whose time has come.
		 */
		wakeUpProcesses();
	}
	wakeUpProcesses();
	done: return rc;
}

static int DiskDriver(void *arg) {
        interruptsOn();
        int unit = (int) arg;
	int status;
	int result;
	int rc = 0;
	while(1) {
		result = P1_WaitDevice(USLOSS_DISK_DEV,unit,&status);
		if (result != 0) {
			rc = 1;
			break;
		}
	}
	return rc;
}

static int TermDriver(void *arg){
	int unit = (int) arg;
	termMB[unit] = P2_MboxCreate(10, P2_MAX_LINE + 1); // Include new line 
	termSem[unit] = P1_SemCreate(1);
	
	P1_V(running);
	return unit;
}

void wakeUpProcesses(void){
	P1_P(clockListSemaphore);
        int i;
       	long now = USLOSS_Clock();
        for(i = 0; i < P1_MAXPROC;i++){
        	if(clockList[i].inUse == 1){
                	if((now - clockList[i].startTime) >= clockList[i].waitingTime){
                       		P1_V(clockList[i].sem);
                                clockList[i].inUse = 0;
                        }
                }
        }
        P1_V(clockListSemaphore);
}

/*
 *  This is the structure used to send a request to
 *  a device.
typedef struct USLOSS_DeviceRequest
{
    int opr;
    void *reg1;
    void *reg2;
} USLOSS_DeviceRequest;
*/

// seek a 512 byte sector from the current track.
int P2_DiskRead(int unit, int track, int first, int sectors, void *buffer) {
	if(permissionCheck() || track < 0 || first < 0 
	|| sectors < 1 || sectors > 15 || unit < 0 || unit > 1){
                return -1;
        }	
	P1_P(diskSem[unit]);
	int status = 0;
	int reg = 0; // disk's status register
	//first make a device request struct for a seek
	USLOSS_DeviceRequest *seekRequest = (USLOSS_DeviceRequest *)malloc(sizeof(USLOSS_DeviceRequest));
	seekRequest->opr = USLOSS_DISK_SEEK;
	seekRequest->reg1 = (void *) track; //track number where the r/w head should be moved
	
	//make device request struct for read
	USLOSS_DeviceRequest *readRequest = (USLOSS_DeviceRequest *)malloc(sizeof(USLOSS_DeviceRequest));
	readRequest->opr = USLOSS_DISK_READ; //type of request
	readRequest->reg1 = (void *)first; //reg1 should contain the index of the sector to be read
	readRequest->reg2 = buffer;
	
	//Now run the device requests
	USLOSS_DeviceOutput(USLOSS_DISK_DEV,unit,seekRequest);
	USLOSS_DeviceInput(USLOSS_DISK_DEV,unit,&status); //status = 2 = ERROR
	//there are 16 tracks (0 based) and we want to read #sectors on the track
	while (first < 16 && sectors != 0 && status != 2) {
		USLOSS_DeviceOutput(USLOSS_DISK_DEV,unit,readRequest);
		first++;
		readRequest->reg1 = (void *) first;
		sectors--;
		USLOSS_DeviceInput(USLOSS_DISK_DEV,unit,&status); //status = 2 = ERROR
	}
	P1_V(diskSem[unit]);
	if (status == 2) {
                return reg; //return disk's status register
        }else {
                return 0;
        }
}
//write a 512 byte sector to the current track
int P2_DiskWrite(int unit, int track, int first, int sectors, void *buffer) {
	if(permissionCheck() || track < 0 || first < 0 || 
	sectors < 1 || sectors > 15 || unit < 0 || unit > 1){
                return -1;
        }
	P1_P(diskSem[unit]);
	int status = 0;
	int reg = 0;	
	 //first make a device request struct for a seek
        USLOSS_DeviceRequest *seekRequest = (USLOSS_DeviceRequest *)malloc(sizeof(USLOSS_DeviceRequest));
        seekRequest->opr = USLOSS_DISK_SEEK;
        seekRequest->reg1 = (void *)track; //track number where the r/w head should be moved

        //make device request struct for write
        USLOSS_DeviceRequest *writeRequest = (USLOSS_DeviceRequest *)malloc(sizeof(USLOSS_DeviceRequest));
        writeRequest->opr = USLOSS_DISK_WRITE; //type of request
        writeRequest->reg1 = (void *)first; 
	//reg1 should contain the index of the sector to write on
        writeRequest->reg2 = buffer;

        //Now run the device requests
        USLOSS_DeviceOutput(USLOSS_DISK_DEV,unit,seekRequest);
	USLOSS_DeviceInput(USLOSS_DISK_DEV,unit,&status); //status = 2 = ERROR
	while (first < 16 && sectors != 0 && status != 2)  {
        	USLOSS_DeviceOutput(USLOSS_DISK_DEV,unit,writeRequest);
	 	first++;
		writeRequest->reg1 = (void *)first;
		sectors--;	
		USLOSS_DeviceInput(USLOSS_DISK_DEV,unit,&status); 
	}
	
	P1_V(diskSem[unit]);	
	if (status == 2) {
		return reg; //return disk's status register, maybe return 2, not sure
	}else {
		return 0;
	}
}

/*P2_DiskSize() returns info about the size of the disk indicated by unit*/
int P2_DiskSize(int unit, int *sector, int *track, int *disk) {
	if(permissionCheck() || unit < 0 || unit > 1){
                return -1;
        }
	P1_P(diskSem[unit]);
	sector = (int *)USLOSS_DISK_SECTOR_SIZE;
	track = (int *)USLOSS_DISK_TRACK_SIZE;
	// Make device request for finding number of tracks in disk
	USLOSS_DeviceRequest *request = (USLOSS_DeviceRequest *)malloc(sizeof(USLOSS_DeviceRequest));
 	request->opr = USLOSS_DISK_TRACKS;
	//reg1 contains pointer to integer where number of disk tracks will be stored
	request->reg1 = &disk;
	USLOSS_DeviceOutput(USLOSS_DISK_DEV,unit,request);
	P1_V(diskSem[unit]);
	return 0;
}

void queueInsert(void *m,int size,message **head) {
	message *node = malloc(sizeof(message));
	node->buf = m;
	node->next = NULL;
	node->bufSize = size;

	if (*head == NULL) {
		*head = node;
		return;
	}

	message *temp = *head;
	while (temp->next != NULL) {
		temp = temp->next;
	}
	temp->next = node;
}

message * queuePop(message **head) {
	message *tmp = *head;
	*head = (*head)->next;
	return tmp;
}

/*
 0 == we are in kernel mode. continue.
 1 == we are not in kernel mode. error message.
 */
static int permissionCheck(void) {
	if ((USLOSS_PsrGet() & 0x1) < 1) {
		USLOSS_Console("Must be in Kernel mode to perform this request. Stopping requested operation\n");
		return 1;
	}
	return 0;
}

static void interruptsOn(void) {
	USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);
}

static void userMode(void) {
	USLOSS_PsrSet(USLOSS_PsrGet() & ~(USLOSS_PSR_CURRENT_MODE));
}

int semAdd(P1_Semaphore s){
	int i;
	P1_P(semGuard);
	for(i = 0; i < P1_MAXSEM;i++){
		if(userSemList[i].inUse == 0){
			userSemList[i].inUse = 1;
			userSemList[i].sem = s;
			P1_V(semGuard);
			return i;
		}
	}
	P1_V(semGuard);
	return -1;
}

int semDelete(int id){
	P1_P(semGuard);
	int ret = P1_SemFree(userSemList[id].sem);
	userSemList[id].inUse = 0;
	P1_V(semGuard);
	return ret;
}

int validSem(int id){
	P1_P(semGuard);
	if(id < 0 || id >= P1_MAXSEM){
		P1_V(semGuard);
		return 0;
	}
	P1_V(semGuard);
	return userSemList[id].inUse == 1;
}
