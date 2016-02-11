/*
 * tsh - A tiny shell program with job control
 *
 * You __MUST__ add your user information here below
 *
 * === User information ===
 * Group: Olafur & Asgeir
 * User 1: olafurks10
 * SSN: 1807825919
 * User 2: asgeira13
 * SSN: 2303882299
 * === End User Information ===
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/*
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv);
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs);
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid);
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid);
int pid2jid(pid_t pid);
void listjobs(struct job_t *jobs);
void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/* My OWN helper functions */
int isNumber(char input[]);

/*
 * main - The shell's main routine
 */
int main(int argc, char **argv)
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
            break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
            break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
            break;
        default:
            usage();
        }
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler);

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {
        /* Read command line */
        if (emit_prompt) {
            printf("%s", prompt);
            fflush(stdout);
        }
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)) {
            app_error("fgets error");
        }
        if (feof(stdin)) { /* End of file (ctrl-d) */
            fflush(stdout);
            exit(0);
        }

        /* Evaluate the command line */
        eval(cmdline);
        fflush(stdout);
        fflush(stdout);
    }

    exit(0); /* control never reaches here */
}

/*
 * eval - Evaluate the command line that the user has just typed in
 *
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard. approx 70 lines
 */
void eval(char *cmdline)
{	
	char *argv[MAXARGS];
	//char buf[MAXLINE];
	int bg;
	pid_t pid;
	// Búum til signal sett og öddum SIGCHLD signal í settið
	sigset_t sigset;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGCHLD);

	//strcpy(buf, cmdline);
	bg = parseline(cmdline, argv);
		
	if(argv[0] == NULL){
		return;
	}
	
	// ef ekki built in command
	if(!builtin_cmd(argv)){
		// remove the signals pointed to by set from the thread mask.
		sigprocmask(SIG_BLOCK, &sigset, NULL);
		if((pid = fork()) == 0){
			setpgrp();
			sigprocmask(SIG_UNBLOCK, &sigset, NULL);
			if(execve(argv[0], argv, environ) < 0){
				// gæti komið villa á þessa print skipun
				printf("%s: Command not found\n", argv[0]);
				fflush(stdout);
				exit(0);
			}
			exit(0);
		}

		// parent waits for foreground job to terminate
		if(!bg) {
			// parent addar child process í job list
			addjob(jobs, pid, FG, cmdline);
			sigprocmask(SIG_UNBLOCK, &sigset, NULL);
			waitfg(pid);
		}
		else{
			addjob(jobs, pid, BG, cmdline);
			int jid; 
			jid = pid2jid(pid);
			printf("[%d] %d %s", jid, pid, cmdline);
			sigprocmask(SIG_UNBLOCK, &sigset, NULL);
		}
	}
	return;
}

/*
 * parseline - Parse the command line and build the argv array.
 *
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.
 */
int parseline(const char *cmdline, char **argv)
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) { /* ignore leading spaces */
        buf++;
    }

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
        buf++;
        delim = strchr(buf, '\'');
    }
    else {
        delim = strchr(buf, ' ');
    }

    while (delim) {
        argv[argc++] = buf;
        *delim = '\0';
        buf = delim + 1;
        while (*buf && (*buf == ' ')) { /* ignore spaces */
            buf++;
        }

        if (*buf == '\'') {
            buf++;
            delim = strchr(buf, '\'');
        }
        else {
            delim = strchr(buf, ' ');
        }
    }
    argv[argc] = NULL;

    if (argc == 0) { /* ignore blank line */
        return 1;
    }

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
        argv[--argc] = NULL;
    }
    return bg;
}

/*
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately. approx 25 lines
 */
int builtin_cmd(char **argv)
{
    char* command = *argv;
    //printf("Value from command: %s\n",command);
	if(!strcmp(command, "quit")){
		// þurfum að tjekka hér hvort það séu einnhver background jobs og bara exita ef það eru engin
		// ef það eru einnhver background jobs, prenta message og returna !
		exit(0);
    }
	else if(!strcmp(command, "fg") || !strcmp(command, "bg")){
		do_bgfg(argv);
		return 1;
    }
	else if(!strcmp(command, "jobs")){
		// eigum bara að lista upp background jobs, held að þetta sé í góðu svona...
		listjobs(jobs);
		return 1;
	}   
	else{
		return 0;
	}
}

int isNumber(char input[]){
	int i;
	for(i = 0; i < strlen(input); i++){
		if(!isdigit(input[i])){
			return 0;
		}
	}
	return 1;
}

/*
 * do_bgfg - Execute the builtin bg and fg commands, approx 50 lines
 */
void do_bgfg(char **argv)
{
	// Error check á input
	if(argv[1] == NULL){
		printf("fg command requires PID or %%jobid argument\n");
		return;
	}

	char in[10];
	memset(&in[0], 0, sizeof(in));
	int isJobId = 0;
	
	if(argv[1][0] == '%'){
		strncpy(in, argv[1]+1, strlen(argv[1])-1);
		isJobId = 1;
	}
	else{
		strncpy(in, argv[1], strlen(argv[1]));
	}
	
	if(!isNumber(in)){	
		printf("fg: argument must be a PID or %%jobid\n");
		return;
	}
	
	// ef argument er process id og finnst ekki í jobs töflu
	if(!getjobpid(jobs, atoi(in)) && !isJobId){
		printf("(%s): No such process\n", in);
		return;
	}
	// ef argument er jobId og finnst ekki í jobs töflu
	else if(!getjobjid(jobs, atoi(in)) && isJobId){
		printf("%%%s: No such job\n", in);
		return;
	}

	struct job_t* job;

	if(isJobId){
		job = getjobjid(jobs, atoi(in));
	}
	else{
		job = getjobpid(jobs, atoi(in));
	}
	
	/* If the state is ST and the command is bg, change the state to BG and send SIGCONT  
	 * signal to the process group. */
	if(job->state == ST && (strcmp(argv[0], "bg") == 0)){
		job->state = BG;
		printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
		//printf("state er stopped og command er bg, setjum running i bg\n");
		// senda SIGCONT signal to process group
		kill(job->pid, SIGCONT);
	}

	/* If the state is ST and the command is fg, change the state to FG, send SIGCONT signal
	 * to the process group and wait*/
	else if(job->state == ST && (strcmp(argv[0], "fg") == 0)){
		job->state = FG;
		// senda SIGCONT á process group og wait með waitfg
		kill(job->pid, SIGCONT);
		waitfg(job->pid);
	}
	/* If the state is BG and the command is fg, change the state to FG and wait
	 * (by calling waitfg) */
	else if(job->state == BG  && (strcmp(argv[0], "fg") == 0)){
		job->state = FG;
		waitfg(job->pid);
	}

    return;
}
/*
 * waitfg - Block until process pid is no longer the foreground process
 * approx 20 lines
 */
void waitfg(pid_t pid)
{	
	// bíðum eftir því að foreground process terminate-ar
	while(pid == fgpid(jobs)){
		sleep(0);
	}
}

/*****************
 * Signal handlers
 *****************/

/*
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate. approx 80 lines
 */
void sigchld_handler(int sig)
{
	int status;
	pid_t pid;
	
	if(verbose){
		printf("Sigchld handler keyrdur\n");
	}
	
	while((pid = waitpid(-1, &status, WNOHANG|WUNTRACED)) > 0){
		if(WIFEXITED(status)){
			deletejob(jobs, pid);
			//printf("Child %d terminated normally with exit status=%d\n", pid, WEXITSTATUS(status));
		}
		else{
			if(WIFSIGNALED(status)){
				printf("Job [%d] (%d) terminated by signal %d\n", pid2jid(pid), pid, WTERMSIG(status));
				deletejob(jobs, pid);
			}
			else if(WIFSTOPPED(status)){
				printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(pid), pid, WTERMSIG(status));
				getjobpid(jobs, pid)->state = ST;
			}
		}
	}
    return;
}

/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job. approx 15 lines
 */
void sigint_handler(int sig)
{
	int pid = fgpid(jobs);
	if(pid == 0)
		return;
	else{
		//int jid = pid2jid(pid);
		//printf("Job [%d] (%d) terminated by signal 2\n", jid, pid);
		kill(-(pid), SIGINT);
	}
}
/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP. approx 15 lines
 */
void sigtstp_handler(int sig)
{
	int pid = fgpid(jobs);
	if(pid == 0) 
		return;
	else{
		//int jid = pid2jid(pid);
		//printf("Job [%d] (%d) stopped by signal 20\n", jid, pid);
		kill(-(pid), SIGTSTP);
	}
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job)
{
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs)
{
    int i;

    for (i = 0; i < MAXJOBS; i++) {
        clearjob(&jobs[i]);
    }
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs)
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].jid > max) {
            max = jobs[i].jid;
        }
    }
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline)
{
    int i;

    if (pid < 1) {
        return 0;
    }

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == 0) {
            jobs[i].pid = pid;
            jobs[i].state = state;
            jobs[i].jid = nextjid++;
            if (nextjid > MAXJOBS)
                nextjid = 1;
            strcpy(jobs[i].cmdline, cmdline);
            if (verbose){
                printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
        }
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid)
{
    int i;

    if (pid < 1) {
        return 0;
    }

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == pid) {
            clearjob(&jobs[i]);
            nextjid = maxjid(jobs)+1;
            return 1;
        }
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].state == FG) {
            return jobs[i].pid;
        }
    }
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1) {
        return NULL;
    }
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == pid) {
            return &jobs[i];
        }
    }
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid)
{
    int i;

    if (jid < 1) {
        return NULL;
    }
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].jid == jid) {
            return &jobs[i];
        }
    }
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid)
{
    int i;

    if (pid < 1) {
        return 0;
    }
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs)
{
    int i;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid != 0) {
            printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
            switch (jobs[i].state) {
            case BG:
                printf("Running ");
                break;
            case FG:
                printf("Foreground ");
                break;
            case ST:
                printf("Stopped ");
                break;
            default:
                printf("listjobs: Internal error: job[%d].state=%d ",
                       i, jobs[i].state);
            }
            printf("%s", jobs[i].cmdline);
        }
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void)
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler)
{
    struct sigaction action, old_action;

    action.sa_handler = handler;
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0) {
        unix_error("Signal error");
    }
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig)
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}
