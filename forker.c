#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <pthread.h>

#define LOG_PREFIX "forker: "

char* stdin_fn;
int stdin_pipe[2];
bool stdin_is_pipe;

void* thread_read_input(void* ptr) {
  char stdin_buf[1024];
  int r;
  int stdin_fd;
  bool stdin_process_closed = false;
  
  do {
    stdin_fd = open(stdin_fn, O_RDONLY);
    while (!stdin_process_closed && (r = read(stdin_fd, stdin_buf, sizeof(stdin_buf))) > 0) {
      r = write(stdin_pipe[1], stdin_buf, r);
      if (r == -1) {
	if (errno == EPIPE) {
	  r = 0;
	  stdin_process_closed = true;
	} else {
	  perror(LOG_PREFIX "Process write error");
	  exit(1);
	}
      }
    }

    if (r == -1) {
      perror(LOG_PREFIX "Standard input source read error");
      exit(1);
    }
    
    close(stdin_fd);
  } while (stdin_is_pipe && !stdin_process_closed);
  close(stdin_pipe[1]);

  return NULL;
}

int main(int argc, char** argv) {
  sigignore(SIGPIPE);
  
  int pid;
  struct stat s_stat;
  char* stdout_fn;
  char* stderr_fn;
  char** exec_params = argv + 4;
  int r;
  
  if (argc < 5) {
    printf(LOG_PREFIX "%s <stdin> <stdout> <stderr> <executable> [params]\n", argv[0]);
    printf(LOG_PREFIX "Runs the given executable in a detached mode, redirecting\nits standard input and output/error to the given file.\n");
    exit(1);
  }

  stdin_fn = argv[1];
  stdout_fn = argv[2];
  stderr_fn = argv[3];
  exec_params = argv + 4;

  //Stat the input 
  if (stat(stdin_fn, &s_stat) == -1) {
    if (errno == ENOENT) {
      if (mkfifo(stdin_fn, 0666) == -1) {
	perror(LOG_PREFIX "mkfifo");
	return 1;
      }

      stdin_is_pipe = true;
    } else {
      perror(LOG_PREFIX "stat");
      return 1;
    }
  } else {
    stdin_is_pipe = S_ISFIFO(s_stat.st_mode);
  }
  
  pid = fork();
  if (pid == -1) {
    //Fork error
    perror(LOG_PREFIX "fork");
    return 1;
  }

  if (pid != 0) {
    //Parent branch
    return 0;
  }

  //Child branch
  pipe(stdin_pipe);
  
  pid = fork(); // Yep, another fork

  if (pid == -1) {
    //Fork error
    perror(LOG_PREFIX "fork");
    return 1;
  }
  
  if (pid == 0) {
    //Child branch
    int stdout_fd = open(stdout_fn, O_WRONLY | O_CREAT | O_APPEND, 0666);
    int stderr_fd = open(stderr_fn, O_WRONLY | O_CREAT | O_APPEND, 0666);

    if (stdout_fd < 0) {
      perror("open");
      return 1;
    }
    
    if (stderr_fd < 0) {
      perror("open");
      return 1;
    }
    
    int prev_stderr = dup(2);
    
    if (stdout_fd != 1) {
      dup2(stdout_fd, 1);
      close(stdout_fd);
    }
    
    if (stderr_fd != 2) {
      dup2(stderr_fd, 2);
      close(stderr_fd);
    }
    
    close(stdin_pipe[1]);
    if (stdin_pipe[0] != 0) {
      dup2(stdin_pipe[0], 0);
      close(stdin_pipe[0]);
    }
    execvp(exec_params[0], exec_params);
    
    dup2(prev_stderr, 2);
    perror(LOG_PREFIX "exec");
    return 1;
  }
  //Parent branch, will read the source file
  //If the file is a FIFO, it will read it continually
  //until the user process has closed its standard input.
  fprintf(stderr, LOG_PREFIX "%s ;; started with pid %d\n", exec_params[0], pid);
  close(stdin_pipe[0]);
  int status;

  pthread_t thd;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_create(&thd, NULL, &thread_read_input, NULL);
  wait(&status);

  fprintf(stderr, LOG_PREFIX "%s ;; terminated pid %d with status %d\n", exec_params[0], pid, WEXITSTATUS(status));
  return WEXITSTATUS(status);
}
