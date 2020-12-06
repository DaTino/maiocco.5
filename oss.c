/*
Alberto Maiocco
CS4760 Project 3
10/20/2020
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <time.h>
#include <math.h>
#include <string.h>


FILE* outfile;

typedef struct msgBuffer {
  long mtype;
  char msgData[32];
}message;

static void interruptHandler();

//this struct might not be a great idea...
typedef struct shareClock{
  int secs;
  int nano;
}shareClock;

enum procStates{New, Ready, Run, Blocked, Terminated};

//need process control table definition
//kk make pcb...gonna try implementing share clock struct instead of
//straight up int pointers to shm
typedef struct PCB {
  shareClock totalCPUTime;
  shareClock totalSystemTime;
  shareClock timeUsedLastBurst;
  enum procStates state;
  int resources[4];
  int simPID;
  int priority;
}pcbType;

pcbType newPCB(shareClock sysClock, int simPID);

typedef struct resourceTable {
  int maxResources[4];
  int procResources[18][4];
}resourceTable;

resourceTable initResTable();

int main(int argc, char *argv[]) {

  //struct timespec tstart={0,0}, tend={0,0};
  //clock_gettime(CLOCK_MONOTONIC, &tstart);

  int maxProc = 5;
  char* filename = "log.txt";
  int maxSecs = 20;
  int vflag = 0;

  int optionIndex;
  while ((optionIndex = getopt(argc, argv, "hvc:l:t:")) != -1) {
    switch (optionIndex) {
      case 'h':
          printf("Welcome to the Valid Argument Usage Dimension\n");
          printf("- = - = - = - = - = - = - = - = - = - = - = - = -\n");
          printf("-h            : Display correct command line argument Usage\n");
          printf("-v            : Verbose- displays program status output as program runs\n");
          printf("-c <int>      : Indicate the maximum total of child processes spawned. (Default 5)\n");
          printf("-l <filename> : Indicate the number of children allowed to exist in the system at the same time. (Default 2)\n");
          printf("-t <int>      : The time in seconds after which the process will terminate, even if it has not finished. (Default 20)\n");
          printf("Usage: ./oss [-h | -c x -l filename -t z]\n");
          exit(0);
        break;

      case 'v':
        vflag = 1;
        break;

      case 'c':
        maxProc = atoi(optarg);
        if (maxProc <= 0) {
          perror("master: maxProc <= 0. Aborting.");
          exit(1);
        }
        if (maxProc > 20) maxProc = 20;
        break;

      case 'l':
        if (optarg == NULL) {
          optarg = "log.txt";
        }
        filename = optarg;
        break;

      case 't':
        maxSecs = atoi(optarg);
        if (maxSecs <= 0) {
          perror("master: maxSecs <= 0. Aborting.");
          exit(1);
        }
        break;

      case '?':
        if(isprint(optopt)) {
          fprintf(stderr, "Uknown option `-%c`.\n", optopt);
          perror("Error: Unknown option entered.");
          return 1;
        }
        else {
          fprintf (stderr, "Unkown option character `\\x%x`.\n", optopt);
          perror("Error: Unknown option character read.");
          return 1;
        }
        return 1;

      default:
        abort();

    }
  }
  printf("getopt test: -v: %d -c: %d -l: %s -t: %d\n", vflag, maxProc, filename, maxSecs);

  //open log file for editing
  outfile = fopen(filename, "a+");
  if (!outfile) {
    perror("oss: error opening output log.");
    exit(1);
  }

  //next step: set up shared memory!
  //We'll do share clock with test values 612 and 6633
  int shmid;
  key_t key = 1337;
  int* shm; // <- this should be the shareClock, right? or its the shared int?
  //create shared mem segment...
  //sizeof int for seconds and nanoseconds... dont need a struct for clock?
  //added size to hold shmPID
  if ((shmid = shmget(key, 3*sizeof(int), IPC_CREAT | 0666)) < 0) {
    perror("oss: error created shared memory segment.");
    exit(1);
  }
  //attach segment to dataspace...
  if ((shm = shmat(shmid, NULL, 0)) == (int*) -1) {
    perror("oss: error attaching shared memory.");
    exit(1);
  }
  //here we'll have to write to shared memory...
  *(shm+0) = 0;
  *(shm+1) = 0;
  //this is the shared int
  *(shm+2) = 0;
  //creating shared memory resource map... start with one process


  //Dear Dr. Hauschild- if you make it this far, listen to The 6th Gate by D-Devils and tell me what you think
  //anyways, Ima set up a  message queue below.

  //create message queue
  message mb;
  mb.mtype = 1;
  strcpy(mb.msgData, "Please work...");
  int msqid;
  key_t msgKey = 612;

  if ((msqid = msgget(msgKey, 0666 | IPC_CREAT)) == -1) {
    perror("oss: error creating message queue.");
    exit(1);
  }

  //Using the interupt handlers...
  // alarm for max time and ctrl-c
  signal(SIGALRM, interruptHandler);
  signal(SIGINT, interruptHandler);
  alarm(5);

  pid_t childpid = 0;
  int status = 0;
  int pid = 0;
  int total = 0;
  int proc_count = 0;
  int nsec = 1000000;
  //send the initial message to get everything going
  if (msgsnd(msqid, &mb, sizeof(mb.msgData), 0) == -1) {
    perror("oss: Message failed to send.");
    exit(1);
  }

  //main looperino right here!
  while (total < 40 /*&& *(shm+0) < maxSecs*/) {
    if((childpid = fork()) < 0) {
      perror("./oss: ...it was a stillbirth.");
      if (msgctl(msqid, IPC_RMID, NULL) == -1) {
           perror("oss: msgctl failed to kill the queue");
           exit(1);
       }
       shmctl(shmid, IPC_RMID, NULL);
      exit(1);
    } else if (childpid == 0) {
      //local clock time stuff here
      *(shm+1) += nsec;
      if (nsec > 1e9) {
        *(shm+0)+=nsec/1e9;
        *(shm+1)-=1e9;
      }
      //printf("oss: Creating new child pid %d at my time %d.%d\n", getpid(), total_sec, nsec_part);
      fprintf(outfile,"oss: Creating new child pid %d at my time %d.%d\n", getpid(), *(shm+0), *(shm+1));
      char *args[]={"./user", NULL};
      execvp(args[0], args);
    } else {
      total++;
      proc_count++;
    }

    //don't want to destroy shm too fast, so we wait for child to finish.
    if(proc_count >= maxProc) {
      do {
        //pid = waitpid(-1, &status, WNOHANG);
        if (*(shm+2) != 0) {
          printf("killed %d\n", *(shm+2));
          fprintf(outfile, "oss: Child pid %d terminated at system clock time %d.%d\n", *(shm+2), *(shm+0), *(shm+1));
          proc_count--;
	      }
      } while(*(shm+2) == 0);
      *(shm+2) = 0;
    }

 }


  printf("total: %d, time: %d.%d\n", total, *(shm+0), *(shm+1));
  //de-tach and de-stroy shm..
  printf("And we're back! shm contains %ds and %dns.\n", *(shm+0), *(shm+1));
  //detach shared mem
  shmdt((void*) shm);
  //delete shared mem
  shmctl(shmid, IPC_RMID, NULL);
  //printf("shm has left us for Sto'Vo'Kor\n");
  if (msgctl(msqid, IPC_RMID, NULL) == -1) {
       perror("oss: msgctl failed to kill the queue");
       exit(1);
   }

   fclose(outfile);

  printf("fin.\n");
  return 0;
}

static void interruptHandler() {
  key_t key = 1337;
  int* shm;
  int shmid;
  if ((shmid = shmget(key, 2*sizeof(int), IPC_CREAT | 0666)) < 0) {
    perror("oss: error created shared memory segment.");
    exit(1);
  }
  if ((shm = shmat(shmid, NULL, 0)) == (int*) -1) {
    perror("oss: error attaching shared memory.");
    exit(1);
  }

  int msqid;
  key_t msgKey = 612;

  if ((msqid = msgget(msgKey, 0666 | IPC_CREAT)) == -1) {
    perror("oss: error creating message queue.");
    exit(1);
  }

  //close file...
  fprintf(outfile, "Interrupt in yo face @ %ds, %dns\n", *(shm+0), *(shm+1));
  fclose(outfile);
  //cleanup shm...
  shmctl(shmid, IPC_RMID, NULL);
  //cleanup shared MEMORY
  if (msgctl(msqid, IPC_RMID, NULL) == -1) {
       perror("oss: msgctl failed to kill the queue");
       exit(1);
   }
  //eliminate any witnesses...
  kill(0, SIGKILL);
  exit(0);
}

pcbType newPCB(shareClock sysClock, int simPID) {
  pcbType pcb;
  pcb.totalCPUTime.secs = 0;
  pcb.totalCPUTime.nano = 0;
  pcb.totalSystemTime.secs = 0;
  pcb.totalSystemTime.nano = 0;
  pcb.timeUsedLastBurst.secs = 0;
  pcb.timeUsedLastBurst.nano = 0;
  pcb.simPID = simPID;
  pcb.state = New;

  int i;
  for (i=0; i<4; i++) {
    pcb.resources[i] = rand() % 10;
  }

  //start by simulating real time class, highest priority
  int weightPriority = rand() % 100 + 1;
  //I'll add the randorinos later, gator.
  if (weightPriority < 20) {
    pcb.priority = 0;
  }
  else pcb.priority = 1;

  return pcb;
}

resourceTable initResTable() {
  resourceTable rt;



  return rt;
}
