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
#include "lib/kernel/bitmap.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void exit_status(int exit_code);
bool validate_input(char* string);
bool validate_pointer(void * ptr);
bool validate_buffer(char* buffer, size_t size);


static void
syscall_handler (struct intr_frame *f UNUSED)
{
  if(!validate_pointer(f->esp)) exit_status(-1);
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
  bool temp;
  switch (*sysnum)
  {
    case SYS_HALT :
      power_off();
      break;

    case SYS_EXIT:
      if(!validate_pointer(f->esp+4)) exit_status(-1);
      exit_code = *((int*) f->esp+1);
      //printf("Exit code: %d\n\n\n\n\n\n", exit_code);
      exit_status(exit_code);
      break;

    case SYS_WAIT:
      if(!validate_pointer(f->esp+4)) exit_status(-1);
      //printf("Syscall WAIT\n");
      //printf("Current tid: %d\n", *((int*)f->esp+4));
      exit_code = process_wait((tid_t) *((int*)f->esp+4));
      //printf("exit code: %d\n", exit_code);
      f->eax = exit_code;
      break;

    case SYS_OPEN:
      if(!validate_pointer(f->esp+4)) exit_status(-1);
      filename = f->esp+4;
      if(!validate_input(*filename)) exit_status(-1);
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
      //printf("Syscall EXEC\n");
      if(!validate_pointer(f->esp+4)) exit_status(-1);
      filename = f->esp+4;
      if(!validate_input(*filename)){
        pid = -1;
      } else {
        //printf("Filename: %s\n", *filename);
        pid = process_execute(*filename);
      }
      f->eax = pid;
      break;

    case SYS_CLOSE:
      if(!validate_pointer(f->esp+4)) exit_status(-1);
      fd = f->esp+4;
      if(2 <= *fd && *fd <= FILE_BITMAP_SIZE+2){  //Can't close anything but allowed FD's.
        *fd-=2;
        file_close(cur_thread->fd[*fd]);  //file_close checks for null pointer.
        cur_thread->fd[*fd] = NULL;
        bitmap_set(cur_thread->fd_map,*fd,false);  //Force the actual FD to be considered closed.
      }
      break;

    case SYS_WRITE:
      if(!validate_pointer(f->esp+4)) exit_status(-1);
      if(!validate_pointer(f->esp+8)) exit_status(-1);
      if(!validate_pointer(f->esp+12)) exit_status(-1);
      fd = f->esp+4;
      if(0 <= *fd && *fd <= FILE_BITMAP_SIZE+2 && *fd != STDIN_FILENO){
        len = f->esp+12;
        if(*fd == STDOUT_FILENO){
          buf = f->esp+8;
          if(!validate_buffer(*buf,*len)) exit_status(-1);
          putbuf(*buf, *len);
          f->eax = *len;
        } else {
          *fd-=2;   //STDIN_FILENO & STDOUT_FILENO is checked for, *fd-=2 won't be smaller than 0.
          filebuf = f->esp+8;
          if(!validate_buffer(*filebuf,*len)) exit_status(-1);
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
    if(!validate_pointer(f->esp+4)) exit_status(-1);
    if(!validate_pointer(f->esp+8)) exit_status(-1);
    filename = f->esp+4;
    if(!validate_input(*filename)) exit_status(-1);
    temp = true;
    size = f->esp+8;
    temp = filesys_create(*filename,(off_t) *size);
    f->eax = temp;
    break;

  case SYS_READ:
  if(!validate_pointer(f->esp+4)) exit_status(-1);
  if(!validate_pointer(f->esp+8)) exit_status(-1);
  if(!validate_pointer(f->esp+12)) exit_status(-1);
    fd = f->esp+4;
    if(0 <= *fd && *fd <= FILE_BITMAP_SIZE+2 && *fd != STDOUT_FILENO){
      len = f->esp+12;
      if(*fd == STDIN_FILENO){
        buf = f->esp+8;
        if(!validate_buffer(*buf,*len)) exit_status(-1);
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
        if(!validate_buffer(*filebuf,*len)) exit_status(-1);
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

bool validate_pointer(void * ptr){
  if(ptr == NULL || !is_user_vaddr(ptr) || pagedir_get_page(thread_current()->pagedir, ptr) == NULL){
    return false;
  }
  return true;
}

bool validate_input(char* string){
  if(!validate_pointer((void*) string)) return false;
  while(*string != '\0'){
    if(string == NULL || string >= PHYS_BASE || pagedir_get_page(thread_current()->pagedir, string) == NULL){
      return false;
    }
    string++;
  }
  return true;
}

bool validate_buffer(char* buffer, size_t size){
  if(!validate_pointer((void*)buffer)) return false;
  size_t temp = 0;
  while(temp < size){
    if((buffer + temp) == NULL || (buffer + temp) >= PHYS_BASE || pagedir_get_page(thread_current()->pagedir, (buffer + temp)) == NULL){
      return false;
    }
    temp++;
  }
  return true;
}

void exit_status(int exit_code){
  if(thread_current()->parent_ci != NULL){
      thread_current()->parent_ci->exit_code = exit_code;
  }
  printf("%s: exit(%d)\n", thread_current()->name, exit_code);
  thread_exit();
}
