/*
CSE321 Lab
Section: 20
Group: 9
Semester: Spring 2026

Project Title: Multithreaded Process Manager Simulator
*/


#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#define MAX_PROCESSES 64
#define LINE_SIZE 100

// Process States
typedef enum{
    RUNNING,
    WAITING,
    ZOMBIE,
    TERMINATED
} State;

// Process Control Blocks: Each simulated process is represented by a PCB structure containing info.
typedef struct{
    int pid;
    int ppid;
    State state;
    int exit_status;
// Child Processes
    int children[MAX_PROCESSES];
    int child_count;
// Waiting/Waking
    pthread_cond_t cond;
} PCB;

// Worker thread info
typedef struct{
    int thread_number;
    char filename[100];
} WorkerInfo;

// Global Process Table: Shared data structure that holds all active processes.
PCB process_table[MAX_PROCESSES];
int process_count = 0;
int next_pid = 1;

// Synchronization
pthread_mutex_t lock;
pthread_cond_t monitor_cond;
pthread_cond_t monitor_done_cond;

// monitor control
// Monitor writes snapshots

int table_changed = 0;          //if process table has changed 
int program_finished = 0;       //if worker threads finished and monitor can exit
char last_action[200];          //save the last write and show in snapshot file

// snapshot file
FILE *snapshot_file = NULL;

// worker thread ids so pm_fork, pm_exit, pm_wait and pm_kill can find which worker invoked.
pthread_t worker_ids[100];
int worker_total = 0;

const char *state_name(State s){
    if (s == RUNNING){
        return "RUNNING";
    }
    if (s == WAITING){
        return "WAITING";
    }
    if (s == ZOMBIE){
        return "ZOMBIE";
    }
    return "TERMINATED";
}

int find_process_index(int pid){
    int i;
    for (i = 0; i < process_count; i++){
        if (process_table[i].pid == pid){
            return i;
        }
    }
    return -1;
}

int get_worker_number(){
    int i;
    pthread_t self_id = pthread_self();
    for (i = 0; i < worker_total; i++){
        if (pthread_equal(self_id, worker_ids[i])){
            return i;
        }
    }
    return -1;
}

void add_child(int parent_pid, int child_pid){
    int parent_index = find_process_index(parent_pid);
    if (parent_index == -1){
        return;
    }
    if (process_table[parent_index].child_count < MAX_PROCESSES){
        process_table[parent_index].children[process_table[parent_index].child_count] = child_pid;
        process_table[parent_index].child_count++;
    }
}

void remove_child(int parent_pid, int child_pid){
    int parent_index = find_process_index(parent_pid);
    int i;
    if (parent_index == -1){
        return;
    }
    for (i = 0; i < process_table[parent_index].child_count; i++){
        if (process_table[parent_index].children[i] == child_pid){
            int j;
            for (j = i; j < process_table[parent_index].child_count - 1; j++){
                process_table[parent_index].children[j] = process_table[parent_index].children[j + 1];
            }
            process_table[parent_index].child_count--;
            break;
        }
    }
}

// Wait until the monitor writes the snapshot.

void monitor_wait(char text[]){
    strcpy(last_action, text);
    table_changed = 1;
    pthread_cond_signal(&monitor_cond);
    while (table_changed == 1){
        pthread_cond_wait(&monitor_done_cond, &lock);
    }
}

void write_snapshot(FILE *out){
    int i;
    fprintf(out, "PID PPID STATE EXIT_STATUS\n");
    fprintf(out, "----------------------------------------------\n");

    for (i = 0; i < process_count; i++){
        if (process_table[i].state != TERMINATED){
            fprintf(out, "%d %d %s ",
                    process_table[i].pid,
                    process_table[i].ppid,
                    state_name(process_table[i].state));

            if (process_table[i].state == ZOMBIE){
                fprintf(out, "%d\n", process_table[i].exit_status);
            } else{
                fprintf(out, "-\n");
            }
        }
    }
    fprintf(out, "\n");
    fflush(out);
}


void initialization(){
    pthread_mutex_init(&lock, NULL);
    pthread_cond_init(&monitor_cond, NULL);
    pthread_cond_init(&monitor_done_cond, NULL);

    process_table[0].pid = 1;
    process_table[0].ppid = 0;
    process_table[0].state = RUNNING;
    process_table[0].exit_status = -1;
    process_table[0].child_count = 0;
    pthread_cond_init(&process_table[0].cond, NULL);

    process_count = 1;
    next_pid = 2;
}

// Functions implemented for Process Management Operations

// pm_fork()

void pm_fork(int parent_pid){
    int parent_index;
    int worker_number;
    char message[200];

    pthread_mutex_lock(&lock);
    if (process_count >= MAX_PROCESSES){
        printf("Process table is full.\n");
        pthread_mutex_unlock(&lock);
        return;
    }
    parent_index = find_process_index(parent_pid);
    if (parent_index == -1){
        printf("Parent process %d not found.\n", parent_pid);
        pthread_mutex_unlock(&lock);
        return;
    }

    process_table[process_count].pid = next_pid;
    process_table[process_count].ppid = parent_pid;
    process_table[process_count].state = RUNNING;
    process_table[process_count].exit_status = -1;
    process_table[process_count].child_count = 0;
    pthread_cond_init(&process_table[process_count].cond, NULL);

    add_child(parent_pid, next_pid);
    process_count++;
    worker_number = get_worker_number();

    if (worker_number == -1){
        sprintf(message, "pm_fork %d", parent_pid);
    } else{
        sprintf(message, "Thread %d calls pm_fork %d", worker_number, parent_pid);
    }
    monitor_wait(message);

    next_pid++;

    pthread_mutex_unlock(&lock);
}

// pm_exit()

void pm_exit(int pid, int status){
    int i;
    int parent_pid;
    int worker_number;
    char message[200];
    pthread_mutex_lock(&lock);

    for (i = 0; i < process_count; i++){
        if (process_table[i].pid == pid){
            if (process_table[i].state == ZOMBIE || process_table[i].state == TERMINATED){
                pthread_mutex_unlock(&lock);
                return;
            }
            process_table[i].state = ZOMBIE;
            process_table[i].exit_status = status;
            parent_pid = process_table[i].ppid;
        // wake the parent if it is waiting
            {
                int j;
                for (j = 0; j < process_count; j++){
                    if (process_table[j].pid == parent_pid){
                        pthread_cond_signal(&process_table[j].cond);
                        break;
                    }
                }
            }
            worker_number = get_worker_number();
            if (worker_number == -1){
                sprintf(message, "pm_exit %d %d", pid, status);
            } else{
                sprintf(message, "Thread %d calls pm_exit %d %d", worker_number, pid, status);
            }
            monitor_wait(message);

            pthread_mutex_unlock(&lock);
            return;
        }
    }
    pthread_mutex_unlock(&lock);
}

// pm_wait()

void pm_wait(int parent_pid, int child_pid){
    int parent_index;
    pthread_mutex_lock(&lock);
    parent_index = find_process_index(parent_pid);
    if (parent_index == -1){
        pthread_mutex_unlock(&lock);
        return;
    }
    while (1){
        int i;
        int found_zombie = -1;
        int matching_child = 0;
        // check if parent has a matching child
        for (i = 0; i < process_count; i++){
            if (process_table[i].ppid == parent_pid){
                if (child_pid == -1 || process_table[i].pid == child_pid){
                    matching_child = 1;
                    if (process_table[i].state == ZOMBIE){
                        found_zombie = i;
                        break;
                    }
                }
            }
        }
        // if no child exists: return
        if (matching_child == 0){
            pthread_mutex_unlock(&lock);
            return;
        }
        // if zombie child exists: remove
        if (found_zombie != -1){
            int pid = process_table[found_zombie].pid;
            int ppid = process_table[found_zombie].ppid;
            int worker_number;
            char message[200];

            process_table[found_zombie].state = TERMINATED;
            remove_child(ppid, pid);
            pthread_cond_destroy(&process_table[found_zombie].cond);
           {
                int j;
                for (j = found_zombie; j < process_count - 1; j++){
                    process_table[j] = process_table[j + 1];
                }
                process_count--;
            }
            worker_number = get_worker_number();
            if (worker_number == -1){
                sprintf(message, "pm_wait %d %d", parent_pid, child_pid);
            } else{
                sprintf(message, "Thread %d calls pm_wait %d %d", worker_number, parent_pid, child_pid);
            }
            monitor_wait(message);
            pthread_mutex_unlock(&lock);
            return;
        }
        // parent waits until a child exits
        process_table[parent_index].state = WAITING;
        pthread_cond_wait(&process_table[parent_index].cond, &lock);
        process_table[parent_index].state = RUNNING;
    }
}

// pm_kill()

void pm_kill(int pid){
    int i;
    int parent_pid;
    int worker_number;
    char message[200];
    pthread_mutex_lock(&lock);

    for (i = 0; i < process_count; i++){
        if (process_table[i].pid == pid){
            if (process_table[i].state == ZOMBIE || process_table[i].state == TERMINATED){
                pthread_mutex_unlock(&lock);
                return;
            }
            process_table[i].state = ZOMBIE;
            process_table[i].exit_status = -1;
            parent_pid = process_table[i].ppid;
            // if parent is waiting: wake/signal
           {
                int j;
                for (j = 0; j < process_count; j++){
                    if (process_table[j].pid == parent_pid){
                        pthread_cond_signal(&process_table[j].cond);
                        break;
                    }
                }
            }
            worker_number = get_worker_number();
            if (worker_number == -1){
                sprintf(message, "pm_kill %d", pid);
            } else{
                sprintf(message, "Thread %d calls pm_kill %d", worker_number, pid);
            }
            monitor_wait(message);
            pthread_mutex_unlock(&lock);
            return;
        }
    }
    pthread_mutex_unlock(&lock);
}

// pm_ps()

void pm_ps()
{
    pthread_mutex_lock(&lock);
    write_snapshot(stdout);
    pthread_mutex_unlock(&lock);
}


// monitor thread

void *monitor(void *arg){
    (void)arg;
    snapshot_file = fopen("snapshots.txt", "w");

    if (snapshot_file == NULL){
        printf("Could not open snapshots.txt\n");
        return NULL;
    }

    pthread_mutex_lock(&lock);
    fprintf(snapshot_file, "Initial Process Table\n");
    write_snapshot(snapshot_file);
    while (1){
        while (table_changed == 0 && program_finished == 0){
            pthread_cond_wait(&monitor_cond, &lock);
        }
        if (table_changed == 1){
            fprintf(snapshot_file, "%s\n", last_action);
            write_snapshot(snapshot_file);
            table_changed = 0;
            pthread_cond_signal(&monitor_done_cond);
        }
        if (program_finished == 1 && table_changed == 0){
            break;
        }
    }
    pthread_mutex_unlock(&lock);
    fclose(snapshot_file);
    return NULL;
}

// worker thread
// script interpreter

void *worker(void *arg){
    WorkerInfo *info = (WorkerInfo *)arg;
    FILE *file;
    char line[LINE_SIZE];
    file = fopen(info->filename, "r");
    if (file == NULL){
        printf("Could not open %s\n", info->filename);
        return NULL;
    }
    while (fgets(line, sizeof(line), file) != NULL){
        int a;
        int b;
        if (sscanf(line, "fork %d", &a) == 1){
            pm_fork(a);
        } else if (sscanf(line, "exit %d %d", &a, &b) == 2){
            pm_exit(a, b);
        } else if (sscanf(line, "wait %d %d", &a, &b) == 2){
            pm_wait(a, b);
        } else if (sscanf(line, "kill %d", &a) == 1){
            pm_kill(a);
        } else if (sscanf(line, "sleep %d", &a) == 1){
            usleep(a * 1000);
        } else if (strncmp(line, "ps", 2) == 0){
            pm_ps();
        }
    }
    fclose(file);
    return NULL;
}

// main function

int main(int argc, char *argv[]){
    int i;
    pthread_t monitor_thread;
    WorkerInfo worker_info[100];

    if (argc < 2){
        printf("Usage: %s thread0.txt thread1.txt ...\n", argv[0]);
        return 1;
    }
    initialization();
    worker_total = argc - 1;
    pthread_create(&monitor_thread, NULL, monitor, NULL);
    for (i = 0; i < worker_total; i++){
        worker_info[i].thread_number = i;
        strcpy(worker_info[i].filename, argv[i + 1]);
        pthread_create(&worker_ids[i], NULL, worker, &worker_info[i]);
    }
    for (i = 0; i < worker_total; i++){
        pthread_join(worker_ids[i], NULL);
    }
    pthread_mutex_lock(&lock);
    program_finished = 1;
    pthread_cond_signal(&monitor_cond);
    pthread_mutex_unlock(&lock);
    pthread_join(monitor_thread, NULL);
    return 0;
}