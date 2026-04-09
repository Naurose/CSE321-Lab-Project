#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

/*Process States using enum*/
typedef enum {
	RUNNING,
	WAITING,
	ZOMBIE
} State;

/*Process Control Block Struct definition*/
typedef struct {
	int pid;
	int ppid;
	State state;
	int exit_status;
	pthread_cond_t cond; //Waiting/Wakeup
} PCB; 

/*Process Table creation*/
#define MAX_PROCESSES 32 /*Cannot have more than 32 processes*/

PCB process_table[MAX_PROCESSES]; /*Global Array*/
int process_count = 0;

/*Pid Counter*/
int next_pid = 1;

/*Mutex for synchronization*/
pthread_mutex_t lock;

void initialization(){
	pthread_mutex_init(&lock, NULL); 
	
	process_table[0].pid = 1;
	process_table[0].ppid = 0;
	process_table[0].state = RUNNING;
	process_table[0].exit_status = -1;
	pthread_cond_init(&process_table[0].cond,NULL);
	
	process_count = 1;
	next_pid = 2;
}

/*Debugging purposes*/
void print_process_table(){
	printf("Pid\tPPid\tState\tExit Status\n");
	printf("-------------------------------\n");
	
	for(int i=0; i<process_count;i++){
		printf("%d\t%d\t%d\t%d\n",
			process_table[i].pid,
			process_table[i].ppid,
			process_table[i].state,
			process_table[i].exit_status
		);
	}
}

//fork(parent_pid)
void pm_fork(int parent_pid){ /*Process Manager fork()*/
	pthread_mutex_lock(&lock);
	//Check if full
	if(process_count > MAX_PROCESSES){
	printf("Process Control Table is full!\n");
	pthread_mutex_unlock(&lock);
	return;
	}
	
	//Create New Process
	PCB new_process;
	new_process.pid = next_pid;
	new_process.ppid = parent_pid;
	new_process.state = RUNNING;
	new_process.exit_status = -1;
	pthread_cond_init(&new_process.cond,NULL);
	
	//Adding to table
	process_table[process_count] = new_process;
	process_count++;
	
	printf("Process %d created with Parent Pid %d\n",next_pid, parent_pid);
	
	next_pid++;
	
	pthread_mutex_unlock(&lock);
}

// exit(target, exit_status)
void pm_exit(int pid, int status){
	pthread_mutex_lock(&lock);
	
	for(int i=0;i<process_count;i++){
		if(process_table[i].pid==pid){
			process_table[i].state = ZOMBIE;
			process_table[i].exit_status = status;
			printf("Process %d exited with status %d\n",pid, status);
			
			//Wake parent
			int parent_pid = process_table[i].ppid;
			for(int j = 0; j<process_count;j++){
				if(process_table[j].pid==parent_pid){
					pthread_cond_signal(&process_table[j].cond);
					break;
				}
			}
			pthread_mutex_unlock(&lock);
			return;
		}
	}
	pthread_mutex_unlock(&lock);
}

//wait(parent_pid,child_pid)
void pm_wait(int parent_pid, int child_pid){
	pthread_mutex_lock(&lock);
	
	while(1){
		int found = -1;
		
		for (int i=0; i<process_count;i++){
			if(process_table[i].ppid==parent_pid){
				if(child_pid==-1||process_table[i].pid==child_pid){
					if(process_table[i].state == ZOMBIE){
					found = i;
					break;
					}
				}
			}
		}
		//Zombie Child 
		if (found != -1){
			int pid = process_table[found].pid;
			int status = process_table[found].exit_status;
			
			printf("Parent %d collected child %d (status %d)\n",
				parent_pid,pid,status);
			//Remove zombie process
			for(int i = found; i<process_count - 1;i++){
				process_table[i]=process_table[i+1];
			}
			process_count--;
			
			pthread_mutex_unlock(&lock);
			return;
		}
		//Otherwise Wait
		for(int i =0;i<process_count;i++){
			if(process_table[i].pid==parent_pid){
				process_table[i].state=WAITING;
				pthread_cond_wait(&process_table[i].cond,&lock);
				
				process_table[i].state = RUNNING;
				break;
			}
		}
		
	}
}

int main(){
	initialization();
	
	pm_fork(1);
	pm_fork(1);
	pm_fork(2);
	
	pm_exit(2,10);
	pm_exit(3,10);
	pm_exit(4,10);
	pm_wait(1,2);
	pm_wait(1,3);
	pm_wait(2,4);
	
	
	print_process_table();
	
	return 0;
}


