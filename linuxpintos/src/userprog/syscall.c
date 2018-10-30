#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/init.h"
#include "filesys/filesys.h"
#include "devices/input.h"
#include "filesys/file.h"
#include "process.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}
void exit_func(int exit_code);
static void
syscall_handler (struct intr_frame *f UNUSED)
{
  //Get syscall number from the stack
  int * sysnum = f->esp;
  int * fd;
  char ** buf;
  void ** filebuf;
  unsigned * len;
  char ** filename;
  off_t * size;
  uint8_t readval;
  size_t id;
  struct file * file;
  int exit_code;
  int pid;
  struct thread * cur_thread = thread_current();
  switch (*sysnum)
  {
    case SYS_HALT :
      power_off();
      break;

    case SYS_EXIT:
      exit_code = (int) f->esp+4;
      exit_func(exit_code);
      break;

    case SYS_WAIT:
      exit_code = process_wait((tid_t) f->esp+4);
      f->eax = exit_code;
      break;

    case SYS_OPEN:
      filename = f->esp+4;
      id = bitmap_scan_and_flip(cur_thread->fd_map,0,1,0);
      if(id != BITMAP_ERROR){
        file = filesys_open(*filename);
        if(file != NULL){
          cur_thread->fd[id] = file;
          f->eax = id+2;
          return;
        }
      }
      f->eax = -1;
      bitmap_flip(cur_thread->fd_map,id);
      break;

    case SYS_EXEC:
      filename = f->esp+4;
      pid = process_execute(*filename);
      break;

    case SYS_CLOSE:
      fd = f->esp+4;
      if(2 <= *fd && *fd <= FILE_BITMAP_SIZE+2){  //Can't close anything but allowed FD's.
        *fd-=2;
        file_close(cur_thread->fd[*fd]);  //file_close checks for null pointer.
        cur_thread->fd[*fd] = NULL;
        bitmap_set(cur_thread->fd_map,*fd,false);  //Force the actual FD to be considered closed.
      }
      break;

    case SYS_WRITE:
      fd = f->esp+4;
      if(0 <= *fd && *fd <= FILE_BITMAP_SIZE+2 && *fd != STDIN_FILENO){
        len = f->esp+12;
        if(*fd == STDOUT_FILENO){
          buf = f->esp+8;
          putbuf(*buf, *len);
          f->eax = *len;
        } else {
          *fd-=2;   //STDIN_FILENO & STDOUT_FILENO is checked for, *fd-=2 won't be smaller than 0.
          filebuf = f->esp+8;
          if(bitmap_test(cur_thread->fd_map,*fd)){
            f->eax = file_write(cur_thread->fd[*fd],*filebuf,*len);
          } else {
            f->eax = -1;
          }
        }
      } else {
        f->eax = -1;
      }
    break;

  case SYS_CREATE:
    filename = f->esp+4;
    size = f->esp+8;
    f->eax = filesys_create(*filename,*size);
    break;

  case SYS_READ:
    fd = f->esp+4;
    if(0 <= *fd && *fd <= FILE_BITMAP_SIZE+2 && *fd != STDOUT_FILENO){
      len = f->esp+12;
      if(*fd == STDIN_FILENO){
        buf = f->esp+8;
        f->eax = *len;
        while(*len != 0){
          readval = input_getc();
          **buf = readval;
          *buf+=1;
          *len-=1;
        }
      } else {
        *fd-=2;  //STDIN_FILENO & STDOUT_FILENO is checked for, *fd-=2 won't be smaller than 0.
        filebuf = f->esp+8;
        if(bitmap_test(cur_thread->fd_map,*fd)){
          f->eax = file_read(cur_thread->fd[*fd],*filebuf,*len);
        } else {
          f->eax = -1;
        }
      }
    } else {
      f->eax = -1;
    }
    break;

  default:
    printf("system call!\n");
    thread_exit();
    break;
  }
}

void exit_func(int exit_code){
  if(thread_current()->parent_ci != NULL){
      thread_current()->parent_ci->exit_code = exit_code;
  }
  printf("%s: exit(%d)\n", thread_current()->name, exit_code);
  thread_exit();
}
