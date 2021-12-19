#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <fcntl.h>
#include <wait.h>
#include <unistd.h>
#include <libgen.h>
#include <errno.h>
#include <string.h>
#include <signal.h>

#define PROC_COUNT (9)
#define STARTING_PROC_ID (1)
#define MAX_CHILDS_COUNT (3)
#define MAX_USR_COUNT (101)

const unsigned char CHILDS_COUNT[PROC_COUNT] = {
    1, 3, 2, 0, 0, 0, 1, 1, 0
};

const unsigned char CHILDS_IDS[PROC_COUNT][MAX_CHILDS_COUNT] = {
    {1},
    {2, 3, 4},
    {5, 6},
    {0},
    {0},
    {0},
    {7},
    {8},
    {0}
};

const unsigned char GROUP_TYPE[PROC_COUNT] = {
    0, 1, 0, 2, 0, 0, 0, 1, 1
};


const char SEND_TO[PROC_COUNT] = {
    0, -6,  1,  0, -2,  0,  4,  4,  4
};

const int SEND_SIGNALS[PROC_COUNT] = {
    0,
    SIGUSR1,
    SIGUSR2,
    0,
    SIGUSR1,
    0,
    SIGUSR1,
    SIGUSR2,
    SIGUSR1
};

const char RECV_SIGNALS_COUNT[2][PROC_COUNT] = {
    { 0, 0, 1, 1, 2, 0, 1, 1, 1 },  /* SIGUSR1 */
    { 0, 1, 0, 0, 1, 0, 0, 0, 0 }   /* SIGUSR2 */
};

void sig_handler(int signum);
void set_sig_handler(void (*handler)(void *), int sig_no, int flags);

char *exec_name = NULL;
void print_error_exit(const char *s_name, const char *msg, const int proc_num);

int proc_id = 0;
void forker(int curr_number, int childs_count);

void kill_wait_for_children();
void wait_for_children();

pid_t *pids_list = NULL;
char *tmp_file_name = "/tmp/pids.log";


int main(int argc, char *argv[]) {
    exec_name = basename(argv[0]);

    pids_list = (pid_t*)mmap(pids_list, (2*PROC_COUNT)*sizeof(pid_t), PROT_READ | PROT_WRITE,
                             MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    int i = 0;                           // initialize pids list with zeros [1]
    for (i = 0; i < 2*PROC_COUNT; ++i) {
        pids_list[i] = 0;
    }

    forker(0, CHILDS_COUNT[0]);          // create processes tree [2]

//    set_sig_handler(&kill_wait_for_children, SIGINT, 0);   // to kill all by Ctrl+C

    set_sig_handler(&kill_wait_for_children, SIGTERM, 0);

    if (proc_id == 0) {                  // main process waits [3]
        wait_for_children();
        munmap(pids_list, (2*PROC_COUNT)*sizeof(pid_t));
        return 0;
    }

    on_exit(&wait_for_children, NULL);

    pids_list[proc_id] = getpid();          // save pid to list

#ifdef DEBUG_PIDS
    printf("%d\tpid: %d\tppid: %d\tpgid: %d\n", proc_id, getpid(), getppid(), getpgid(0));
    fflush(stdout);
#endif

    if (proc_id == STARTING_PROC_ID) {                  // starter waits for all pids to be available [3]
        do{
            for (i = 1; (i <= PROC_COUNT) && (pids_list[i] != 0); ++i) {

                if (pids_list[i] == -1) {
                    print_error_exit(exec_name, "Error: not all processes forked or initialized.\nHalting.", 0);
                    exit(1);
                }
            }
//            printf("%d\t", i);
        } while (i < PROC_COUNT);

#ifdef DEBUG_PIDS
        printf("All pids are set!\n");
        for (i = 0; (i < 2*PROC_COUNT); ++i) {
            printf("%d - %d\n", i, pids_list[i]);
            fflush(stdout);
        }
#endif

        FILE *tmp = fopen(tmp_file_name, "wt");
        if (tmp == NULL) {
            print_error_exit(exec_name, "Can't create temp file", 0);
        }

        for (i = 1; i < PROC_COUNT; ++i) {
            fprintf(tmp, "%d\n", pids_list[i]);
        }

        fclose(tmp);

        pids_list[0] = 1;           // all pids are set

        set_sig_handler(&sig_handler, 0, 0);

        do{
            for (i = 1+PROC_COUNT; (i < 2*PROC_COUNT)  && (pids_list[i] != 0); ++i) {

                if (pids_list[i] == -1) {
                    print_error_exit(exec_name, "Error: not all processes forked or initialized.\nHalting.", 0);
                    exit(1);
                }
            }
//            printf("%d\t", i);
        } while (i < 2*PROC_COUNT);

        for (i = PROC_COUNT+1; i < 2*PROC_COUNT; ++i) {     /*  reset flags  */
            pids_list[i] = 0;
        }

#ifdef DEBUG_HANDS
        printf("All handlers are set!\n");
        for (i = 0; (i < 2*PROC_COUNT); ++i) {
            printf("%d - %d\n", i, pids_list[i]);
            fflush(stdout);
        }
        puts("==================================");
#endif

        sig_handler(0); // start signal-flow

    } else {    // other processes

        do {
            // wait for all pids to be written
        } while (pids_list[0] == 0);

        set_sig_handler(&sig_handler, 0, 0);
    }

    while (1) {
        pause();  // start cycle
    }

    return 0;
}   /*  main   */


void print_error_exit(const char *s_name, const char *msg, const int proc_num) {
    fprintf(stderr, "%s: %s %d\n", s_name, msg, proc_num);
    fflush(stderr);

    pids_list[proc_num] = -1;

    exit(1);
}   /*  print_error */


void wait_for_children() {
    int i = CHILDS_COUNT[proc_id];
    while (i > 0) {
        wait(NULL);
        --i;
    }
}   /*  wait_for_children  */


/* ====================================================== */

long long current_time() {
    struct timeval time;
    gettimeofday(&time, NULL);

    return time.tv_usec;
}   /*  current_time  */


/*                U1, U2  */
volatile int usr_recv[2] = {0, 0};

volatile int usr_amount[2][2] =
{
/*   r, s   */
    {0, 0}, /* SIGUSR1 */
    {0, 0}  /* SIGUSR2 */
};


void kill_wait_for_children() {
    int i = 0;

    for (i = 0; i < CHILDS_COUNT[proc_id]; ++i) {
#ifdef DEBUG_PIDS
        printf("sigint -> %d\n", CHILDS_IDS[proc_id][i]);
        fflush(stdout);
#endif
        kill(pids_list[CHILDS_IDS[proc_id][i]], SIGTERM);
    }

    wait_for_children();

    if (proc_id != 0)
#ifdef DEBUG_SIGS
        printf("%lld %d was terminated after %d SIGUSR1 and %d SIGUSR2\n",
               current_time(), proc_id, usr_amount[0][1], usr_amount[1][1]);
#else
    printf("%d %d завершил работу после %d SIGUSR1 и %d SIGUSR2\n",
#ifndef RECV
           getpid(), getppid(), usr_amount[0][1], usr_amount[1][1]);
#else
           getpid(), getppid(), usr_amount[0][0], usr_amount[1][0]);
#endif
    fflush(stdout);

#endif
    exit(0);
}   /*  kill_wait_for_children  */


void sig_handler(int signum) {

    if (signum == SIGUSR1) {
        signum = 0;
    } else if (signum == SIGUSR2) {
        signum = 1;
    } else {
        signum = -1;
    }

    if (signum != -1) {
        ++usr_amount[signum][0];
        ++usr_recv[signum];
#ifdef DEBUG_SIGS
        printf("%lld %d received %s%d\n", current_time(), proc_id,
               "USR", signum+1 );
#else
        printf("%d %d %d получил %s%d %lld\n", proc_id, getpid(), getppid(),
               "USR", signum+1, current_time() );
#endif
        fflush(stdout);

#ifdef DEBUG_SIGS
    printf("%lld %d has %d:%d\n", current_time(), proc_id, usr_recv[0], usr_recv[1]);
    fflush(stdout);
#endif

    // for 2-10 variant only:
    if (proc_id == 1) {
        if (usr_amount[0][0] + usr_amount[1][0] == MAX_USR_COUNT) {
            kill_wait_for_children();
        }

        pids_list[PROC_COUNT + 6] = pids_list[PROC_COUNT + 4] = 0;
    }

    if (proc_id == 8) {
        do{
            // wait for 6th and 4th processes to send signal
        }while ( (pids_list[PROC_COUNT + 6] + pids_list[PROC_COUNT + 4]) != 2 );
    }

    if (! ( (usr_recv[0] == RECV_SIGNALS_COUNT[0][proc_id]) &&
        (usr_recv[1] == RECV_SIGNALS_COUNT[1][proc_id]) ) ) {
#ifdef DEBUG_SIGS
            printf("%lld %d not enough!\n",current_time(), proc_id);
            fflush(stdout);
#endif
            if ( (proc_id == 4) && (signum == 0) ) {
                pids_list[PROC_COUNT + 4] = 1;
            }

            return;
        }
        usr_recv[0] = usr_recv[1] = 0;
    }

    char to = SEND_TO[proc_id];

    if (to != 0) {
        signum = ( (SEND_SIGNALS[proc_id] == SIGUSR1) ? 1 : 2);
        ++usr_amount[signum-1][1];
    }

#ifdef DEBUG_SIGS
    printf("%lld %d sent %s%d\n", current_time(), proc_id,
           "USR", signum);
#else
    printf("%d %d %d послал %s%d %lld\n", proc_id, getpid(), getppid(),
           "USR", signum, current_time() );
#endif
    fflush(stdout);

    if (to > 0) {
        kill(pids_list[to], SEND_SIGNALS[proc_id]);
    } else if (to < 0) {
        kill(-getpgid(pids_list[-to]), SEND_SIGNALS[proc_id]);
    } else {
        return;
    }

    // for 2-10 variant only:
    if (proc_id == 6) {
        pids_list[PROC_COUNT + 6] = 1;
    }

}   /*  handler  */


void set_sig_handler(void (*handler)(void *), int sig_no, int flags) {
    struct sigaction sa, oldsa;             // set sighandler [4]

    sigset_t block_mask;
    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGUSR1);
    sigaddset(&block_mask, SIGUSR2);

    sa.sa_mask = block_mask;
    sa.sa_flags = flags;
    sa.sa_handler = handler;

    if (sig_no != 0) {
        sigaction(sig_no, &sa, &oldsa);
        return;
    }

    int i = 0;
    for (i = 0; i < PROC_COUNT; ++i) {
        char to = SEND_TO[i];
                                                                         /* someone sends me a signal: */
        if ( ( (to > 0) && (to == proc_id) )  ||                             /* <- directly */
             ( (to < 0) && (getpgid(pids_list[-to]) == getpgid(0)) ) ) {      /* <- or through the group */

            if (SEND_SIGNALS[i] != 0) { // signal is really sent
                if (sigaction(SEND_SIGNALS[i], &sa, &oldsa) == -1) {
                    print_error_exit(exec_name, "Can't set sighandler!", proc_id);
                }
#ifdef DEBUG_HANDS
                printf("%d) will receive a signal %d from %d\n", proc_id, SEND_SIGNALS[i], i);
                fflush(stdout);
#endif
            }
        }
    }   /* for */

    pids_list[proc_id + PROC_COUNT] = 1;

}   /*  set_sig_handler  */


void forker(int curr_number, int childs_count) {
    pid_t pid = 0;
    int i = 0;

    for (i = 0; i < childs_count; ++i) {

        int chld_id = CHILDS_IDS[curr_number][i];
        if ( (pid = fork() ) == -1) {

            print_error_exit(exec_name, "Can't fork!", chld_id);

        } else if (pid == 0) {  /*  child    */
            proc_id = chld_id;

            if (CHILDS_COUNT[proc_id] != 0) {
                forker(proc_id, CHILDS_COUNT[proc_id]);         // fork children
            }

            break;

        } else {    // pid != 0 (=> parent)
            static int prev_chld_grp = 0;

            int grp_type = GROUP_TYPE[chld_id];

            if (grp_type == 0) {
                if (setpgid(pid, pid) == -1) {
                    print_error_exit(exec_name, "Can't set group", chld_id);
                } else {
                    prev_chld_grp = pid;
                }

            } else if (grp_type == 1) {
                if (setpgid(pid, getpgid(0)) == -1) {
                    print_error_exit(exec_name, "Can't set group", chld_id);
                }
            } else if (grp_type == 2) {
                if (setpgid(pid, prev_chld_grp) == -1) {
                    print_error_exit(exec_name, "Can't set group", chld_id);
                }
            }

        }   // parent branch

    }   // for

}   /*  forker  */
