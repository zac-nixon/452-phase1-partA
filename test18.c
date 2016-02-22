#include "phase1.h"
#include <stdio.h>
#include <stdlib.h>
 
 

int count = 0; 
int P3_Startup(void *notused);
int P4_Startup(void *notused);
int P5_Startup(void *notused);
int P6_Startup(void *notused);
int P7_Startup(void *notused);
int P8_Startup(void *notused);

int P2_Startup(void *notused) 
{
    USLOSS_Console("P2_Startup\n");
    P1_Fork("P3_Startup", P3_Startup, NULL, 4 *  USLOSS_MIN_STACK, 4);
    USLOSS_Console("P2_Finished\n");
    return 0;
}
 
int P3_Startup(void *notused) 
{
    	USLOSS_Console("P3_Startup\n");
    	P1_Fork("P5_Startup", P5_Startup, NULL, 4 *  USLOSS_MIN_STACK, 5);
 	int i;
	for (i = 0; i < 40; i++) {
    		P1_Fork("P4_Startup", P4_Startup, NULL, 4 *  USLOSS_MIN_STACK, 5);
	}	
	P1_DumpProcesses();
	int status = 2;
	int val = P1_Join(&status);
	printf("P1_Join return value: %d\n", val);
	P1_DumpProcesses();
    	P1_Fork("P8_Startup", P8_Startup, NULL, 4 *  USLOSS_MIN_STACK, 3);
	USLOSS_Console("P3_Finished\n");
	return 0; 
}
 
int P4_Startup(void *notused) {
 	//   P1_DumpProcesses();
    	USLOSS_Console("P4_Startup\n");
    	USLOSS_Console("P4_Finished\n");
   	return 0;
}


int P5_Startup(void *notused)
{
    	USLOSS_Console("P5_Startup\n");
	P1_Fork("P6_Startup", P6_Startup, NULL, 4 *  USLOSS_MIN_STACK, 1);
 	USLOSS_Console("P5_Finished\n");
	P1_DumpProcesses();
   	return 0;
}


int P6_Startup(void *notused)
{
    USLOSS_Console("P6_Startup\n");
    USLOSS_Console("P6_Finished\n");
    return 0;
}


int P7_Startup(void *notused)
{
    USLOSS_Console("P7_Startup\n");
    USLOSS_Console("P7_Finished\n");
    return 0;
}


int P8_Startup(void *notused)
{
    USLOSS_Console("P8_Startup\n");
    USLOSS_Console("P8_Finished\n");
    return 0;
}
 
void setup(void) {
   // do nothing
}
 
void cleanup(void) {
    // Do nothing.
}
