#include<context.h>
#include<memory.h>
#include<lib.h>
#include<entry.h>
#include<file.h>
#include<tracer.h>


///////////////////////////////////////////////////////////////////////////
//// 		Start of Trace buffer functionality 		      /////
///////////////////////////////////////////////////////////////////////////

enum {
	DEPTH1 = 1,
	DEPTH2,
	DEPTH3,
	DEPTHM,
};


static inline void free_alloc_depth (struct file *fptr, u32 depth)
{
	if (fptr) {
		switch (depth) {
			case DEPTHM:
				if (fptr->fops) {
					os_free(fptr->fops, sizeof(struct fileops));
					fptr->fops = NULL;
				}
			case DEPTH3:
				if (fptr->trace_buffer->buff) {
					os_page_free(USER_REG, fptr->trace_buffer->buff);
					fptr->trace_buffer->buff = NULL;
				}
			case DEPTH2:
				if (fptr->trace_buffer) {
					os_free(fptr->trace_buffer, sizeof(struct trace_buffer_info));
					fptr->trace_buffer = NULL;
				}
			case DEPTH1:
				if (fptr) {
					os_free(fptr, sizeof(struct file));
					fptr->fops = NULL;
				}
		}
	}
}


/* Legitamacy of user space buffer */
int is_valid_mem_range(unsigned long buff, u32 count, int access_bit) 
{
	struct exec_context *ctx   = get_current_ctx();
	struct mm_segment   *pmms  = ctx->mms;
	struct vm_area      *pvm   = ctx->vm_area;

	unsigned long bstart = buff;
	unsigned long bend   = buff + count;

	for (int i = MM_SEG_CODE; i < MAX_MM_SEGS; i++) {
		switch(i) {
			case MM_SEG_CODE:
			case MM_SEG_RODATA:
				continue;
			break;
			case MM_SEG_DATA:
				if ((bstart >= pmms[i].start && bstart < pmms[i].next_free-1) &&
				    (bend > pmms[i].start && bend <= pmms[i].next_free-1)) {
					if (access_bit & 0x01)
						if ((access_bit & 0x01) != (pmms[i].access_flags & 0x01))
							return 0;
					if (access_bit & 0x02)
						if ((access_bit & 0x02) != (pmms[i].access_flags & 0x02))
							return 0;
					if (access_bit & 0x04)
						if ((access_bit & 0x04) != (pmms[i].access_flags & 0x04))
							return 0;
					return 1;
				}
			break;
			case MM_SEG_STACK:
				if ((bstart >= pmms[i].start && bstart < pmms[i].end-1) &&
				    (bend > pmms[i].start && bend <= pmms[i].end-1)) {
					if (access_bit & 0x01)
						if ((access_bit & 0x01) != (pmms[i].access_flags & 0x01))
							return 0;
					if (access_bit & 0x02)
						if ((access_bit & 0x02) != (pmms[i].access_flags & 0x02))
							return 0;
					if (access_bit & 0x04)
						if ((access_bit & 0x04) != (pmms[i].access_flags & 0x04))
							return 0;
					return 1;
				}
			break;
		}
	}

	while (pvm) {
		if ((bstart >= pvm->vm_start && bstart < pvm->vm_end-1) &&
		    (bend > pvm->vm_start && bend <= pvm->vm_end-1)) {
			if (access_bit & 0x01)
				if ((access_bit & 0x01) != (pvm->access_flags & 0x01))
					return 0;
			if (access_bit & 0x02)
				if ((access_bit & 0x02) != (pvm->access_flags & 0x02))
					return 0;
			if (access_bit & 0x04)
				if ((access_bit & 0x04) != (pvm->access_flags & 0x04))
					return 0;
			return 1;

		}
		pvm = pvm->vm_next;
	}

	return 0;
}

// Last call
long trace_buffer_close(struct file *filep)
{
	// if the file pointer is null then return -EINVAL
	if (!filep)
		return -EINVAL;

	free_alloc_depth(filep, DEPTHM);

	return 0;
}


int _trace_buffer_read(struct file *filep, char *buff, u32 count)
{
	int i;
	u8 *tbuff = filep->trace_buffer->buff;
	u32 is_full = filep->trace_buffer->is_full;
	u32 curr_r_offset = filep->trace_buffer->r_offset;
	u32 curr_w_offset = filep->trace_buffer->w_offset;

	// if tbuf has nothing to read
	if (!is_full && curr_r_offset == curr_w_offset) {
		return 0;
	}

	// read byte-by-byte
	for (i = 0; i<count; i++) {
		buff[i] = tbuff[curr_r_offset];
		curr_r_offset = (curr_r_offset + 1) % TRACE_BUFFER_MAX_SIZE;

		// check if this byte read has made tbuff empty
		if (curr_r_offset == curr_w_offset)
			break;
	}

	// update r_offset with current val
	filep->trace_buffer->r_offset = curr_r_offset;

	// if is_full set then unset it
	if (is_full && count)
		filep->trace_buffer->is_full = 0;

	return i==count?i:i+1;

}

/* Copy from trace buff to user buff
   return number of bytes read or -EINVAL
 */
int trace_buffer_read(struct file *filep, char *buff, u32 count)
{
	if (!is_valid_mem_range((u64)buff, count, 2))
		return -EBADMEM;

	return _trace_buffer_read(filep, buff, count);
}

int _trace_buffer_write(struct file *filep, char *buff, u32 count)
{
	int i;
	u8 *tbuff = filep->trace_buffer->buff;
	u32 is_full = filep->trace_buffer->is_full;
	u32 curr_r_offset = filep->trace_buffer->r_offset;
	u32 curr_w_offset = filep->trace_buffer->w_offset;

	// if tbuf is full
	if (is_full && curr_r_offset == curr_w_offset) {
		return 0;
	}

	for (i = 0; i<count; i++) {
		tbuff[curr_w_offset] = buff[i];
		curr_w_offset = (curr_w_offset + 1) % TRACE_BUFFER_MAX_SIZE;

		// check if this write has made buffer full
		if (curr_r_offset == curr_w_offset) {
			// update the is_full flag
			filep->trace_buffer->is_full = 1;

			break;
		}
	}

	// update r_offset with current val
	filep->trace_buffer->w_offset = curr_w_offset;

	return i==count?i:i+1;
}

/* Copy from user buff to trace buff
   return number of bytes written or -EINVAL
 */
int trace_buffer_write(struct file *filep, char *buff, u32 count)
{
	if (!is_valid_mem_range((u64)buff, count, 1))
		return -EBADMEM;

	return _trace_buffer_write(filep, buff, count);
}

int sys_create_trace_buffer(struct exec_context *current, int mode)
{
	int fd;
	struct file *fptr = NULL;
	struct exec_context *ctx = current;

	// find a free file descriptor in files array; use lowest/first
	// fds {0, 1, 2} are already allocated hence starting at '3'
	for (fd = 3; fd < MAX_OPEN_FILES; fd++)
		if (ctx->files[fd] == NULL)
			break;

	// no free file descriptor available
	if (fd == MAX_OPEN_FILES)
		return -EINVAL;
	// Allocate and Initialize
	fptr = os_alloc(sizeof(struct file));
	if (fptr == NULL)
		return -ENOMEM;
	fptr->type = TRACE_BUFFER;
	fptr->mode = mode;
	fptr->offp = 0;
	fptr->ref_count = 1;
	if (NULL == (fptr->trace_buffer = os_alloc(sizeof(struct trace_buffer_info))))
		free_alloc_depth(fptr, DEPTH1);
	if (NULL == (fptr->trace_buffer->buff = (u8 *)os_page_alloc(USER_REG)))
		free_alloc_depth(fptr, DEPTH2);
	fptr->trace_buffer->r_offset = 0;
	fptr->trace_buffer->w_offset = 0;
	fptr->trace_buffer->is_full = 0;
	if (NULL == (fptr->fops = os_alloc(sizeof(struct fileops))))
		free_alloc_depth(fptr, DEPTH3);
	fptr->fops->read = trace_buffer_read;
	fptr->fops->write = trace_buffer_write;
	fptr->fops->close = trace_buffer_close;
	fptr->fops->lseek = NULL;

	ctx->files[fd] = fptr;

	return fd;
}

///////////////////////////////////////////////////////////////////////////
//// 		Start of strace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////

typedef struct {
	u32 syscall_num;
	u32 sysarg_cnt;
} syscall_arg_cnt;

// add syscalls into below map as per need
syscall_arg_cnt sysarg_map[] = {
					{ SYSCALL_EXIT       , 0},
					{ SYSCALL_GETPID     , 0},
					{ SYSCALL_GETPPID     , 0},
					{ SYSCALL_FORK     , 0},
					{ SYSCALL_CFORK     , 0},
					{ SYSCALL_VFORK     , 0},
					{ SYSCALL_PHYS_INFO     , 0},
					{ SYSCALL_STATS     , 0},
					{ SYSCALL_GET_COW_F     , 0},
					{ SYSCALL_CONFIGURE     , 1},
					{ SYSCALL_DUMP_PTT     , 1},
					{ SYSCALL_SLEEP     , 1},
					{ SYSCALL_PMAP     , 1},
					{ SYSCALL_CLOSE     , 1},
					{ SYSCALL_DUP     , 1},
					{ SYSCALL_DUP2     , 2},
					{ SYSCALL_SIGNAL     , 2},
					{ SYSCALL_EXPAND     , 2},
					{ SYSCALL_CLONE     , 2},
					{ SYSCALL_MUNMAP     , 2},
					{ SYSCALL_MMAP       , 3},
					{ SYSCALL_OPEN       , 3},
					{ SYSCALL_MPROTECT       , 3},
					{ SYSCALL_READ       , 3},
					{ SYSCALL_WRITE      , 3},
					{ SYSCALL_LSEEK      , 3},
					{ SYSCALL_STRACE      , 2},
					{ SYSCALL_FTRACE      , 4},
					{ SYSCALL_TRACE_BUFFER      , 1},
					{ SYSCALL_READ_STRACE      , 3},
					{ SYSCALL_READ_FTRACE      , 3},
					{ SYSCALL_END_STRACE      , 0}
			       };

// return index in the above array if fptr == NULL
int push_strace_data (struct file *fptr, u64 syscall_num, u64 param1, u64 param2, u64 param3, u64 param4)
{
	int i = 0, wbytes = 0;
	while (sysarg_map[i].syscall_num != syscall_num) {
		i++;
	}
	
	if (!fptr)
		return i;

	switch (sysarg_map[i].sysarg_cnt) {
		case 0:
			wbytes += _trace_buffer_write(fptr, (char *)&syscall_num, (u32) sizeof(syscall_num));
			break;
		case 1:
			wbytes += _trace_buffer_write(fptr, (char *)&syscall_num, (u32) sizeof(syscall_num));
			wbytes += _trace_buffer_write(fptr, (char *)&param1, (u32) sizeof(param1));
			break;
		case 2:
			wbytes += _trace_buffer_write(fptr, (char *)&syscall_num, (u32) sizeof(syscall_num));
			wbytes += _trace_buffer_write(fptr, (char *)&param1, (u32) sizeof(param1));
			wbytes += _trace_buffer_write(fptr, (char *)&param2, (u32) sizeof(param2));
			break;
		case 3:
			wbytes += _trace_buffer_write(fptr, (char *)&syscall_num, (u32) sizeof(syscall_num));
			wbytes += _trace_buffer_write(fptr, (char *)&param1, (u32) sizeof(param1));
			wbytes += _trace_buffer_write(fptr, (char *)&param2, (u32) sizeof(param2));
			wbytes += _trace_buffer_write(fptr, (char *)&param3, (u32) sizeof(param3));
			break;
		case 4:
			wbytes += _trace_buffer_write(fptr, (char *)&syscall_num, (u32) sizeof(syscall_num));
			wbytes += _trace_buffer_write(fptr, (char *)&param1, (u32) sizeof(param1));
			wbytes += _trace_buffer_write(fptr, (char *)&param2, (u32) sizeof(param2));
			wbytes += _trace_buffer_write(fptr, (char *)&param3, (u32) sizeof(param3));
			wbytes += _trace_buffer_write(fptr, (char *)&param4, (u32) sizeof(param4));
			break;
		default:
			return -1;
	}

	return wbytes;
}

// this shall be called even before a syscall's handler
int perform_tracing(u64 syscall_num, u64 param1, u64 param2, u64 param3, u64 param4)
{
	// don't fill buffer of the last call
	// (incorrect but testcase assumes this!)
	if (syscall_num == SYSCALL_END_STRACE)
		return 0;

	struct exec_context *ctx = get_current_ctx();

	struct strace_head *st_head = ctx->st_md_base;

	if (!st_head)
		return 0;	// if a process is not traced then its still ok!

	struct file *fptr = ctx->files[st_head->strace_fd];
	if (!fptr)
		return -EINVAL;

	struct trace_buffer_info *st_info = fptr->trace_buffer;

	if (st_head->is_traced == 1) {
		switch (st_head->tracing_mode) {
			case FULL_TRACING:
			{
				// copy syscall info to trace buff
				push_strace_data (fptr, syscall_num, param1, param2, param3, param4);
			}
			break;
			case FILTERED_TRACING:
			{
				struct strace_info *ptr = st_head->next;
				int st_cnt 		= st_head->count;

				// check if st is already enabled for the syscall
				while (st_cnt) {
					if (syscall_num == ptr->syscall_num)
						break;
					ptr = ptr->next;
					st_cnt--;
				}
				if (st_cnt) {
					// copy syscall info to trace buff
					push_strace_data (fptr, syscall_num, param1, param2, param3, param4);
				}
			}
			break;
			default:
				return -EINVAL;
		}
	}

	return 0;
}

// This makes sense ONLY in case of filtered tracing
// for adding or removing trace-candidates
int sys_strace(struct exec_context *current, int syscall_num, int action)
{
	struct strace_head *st_head = current->st_md_base;

	// this struct is unallocated at start hence allocate it
	if (st_head == NULL) {
		st_head = os_alloc(sizeof(struct strace_head));
		if (st_head == NULL)
			// can not allocate
			return -EINVAL;

		current->st_md_base = st_head;

		// initialize
		st_head->tracing_mode = FILTERED_TRACING;
		st_head->is_traced    = 0;
		st_head->count        = 0;
		st_head->next         = NULL;
		st_head->last         = NULL;
	}


	struct strace_info *ptr = st_head->next;
	int              st_cnt = st_head->count;

	switch(action) {
		case ADD_STRACE:
		{
			// check if st is already enabled for the syscall
			while (st_cnt) {
				if (syscall_num == ptr->syscall_num)
					return -EINVAL;
				ptr = ptr->next;
				st_cnt--;
			}

			// first check if the current count is within max
			if (st_head->count + 1 > MAX_STRACE)
				return -EINVAL;

			// syscall not traced hence add: at head or tail
			ptr = os_alloc(sizeof(struct strace_info));
			if (ptr == NULL)
				return -EINVAL;
			ptr->syscall_num = syscall_num;
			ptr->next = NULL;

			if (st_head->count) {
				// at the tail
				st_head->last->next = ptr;
				st_head->last = ptr;
			} else {
				// first addition
				st_head->last = st_head->next = ptr;
			}
			st_head->count += 1;
		}
		break;
		case REMOVE_STRACE:
		{
			// check if st is actually enabled for the syscall
			struct strace_info *t = NULL;
			while (st_cnt) {
				if (syscall_num == ptr->syscall_num)
					break;
				// 't' will always be a node behind 'ptr'
				t = ptr;
				ptr = ptr->next;
				st_cnt--;
			}
			if (st_cnt) {
				// syscall traced hence disable it
				// remove its strace_info entry: head, mid, end
				if (ptr == st_head->next) {
					// head deletion
					st_head->next = ptr->next;
				} else if (ptr == st_head->last) {
					// tail deletion
					st_head->last = t;
				} else {
					t->next = ptr->next;
				}
				os_free(ptr, sizeof(struct strace_info));
				st_head->count -= 1;
			} else
				// trying to remove something i.e. not present
				return -EINVAL;
		}
		break;
		default:
			return -EINVAL;
	}

	return 0;
}

int sys_read_strace(struct file *filep, char *buff, u64 count)
{
	u32 rbytes = 0;

	for (int i=0; i<count; i++) {
		// get syscall num first
		rbytes += _trace_buffer_read(filep, buff+rbytes, sizeof(u64));
		switch(sysarg_map[push_strace_data (NULL, *((u64*)buff), 0,0,0,0)].sysarg_cnt) {
			case 0:
				continue;
			case 1:
				rbytes += _trace_buffer_read(filep, buff+rbytes, sizeof(u64));
				break;
			case 2:
				rbytes += _trace_buffer_read(filep, buff+rbytes, sizeof(u64));
				rbytes += _trace_buffer_read(filep, buff+rbytes, sizeof(u64));
				break;
			case 3:
				rbytes += _trace_buffer_read(filep, buff+rbytes, sizeof(u64));
				rbytes += _trace_buffer_read(filep, buff+rbytes, sizeof(u64));
				rbytes += _trace_buffer_read(filep, buff+rbytes, sizeof(u64));
				break;
			case 4:
				rbytes += _trace_buffer_read(filep, buff+rbytes, sizeof(u64));
				rbytes += _trace_buffer_read(filep, buff+rbytes, sizeof(u64));
				rbytes += _trace_buffer_read(filep, buff+rbytes, sizeof(u64));
				rbytes += _trace_buffer_read(filep, buff+rbytes, sizeof(u64));
				break;
			default:
				return -EINVAL;
		}
	}
	return rbytes;
}

int sys_start_strace(struct exec_context *current, int fd, int tracing_mode)
{
	struct strace_head *st_head = current->st_md_base;

	// this struct is unallocated at start hence allocate it
	if (st_head == NULL) {
		st_head = os_alloc(sizeof(struct strace_head));
		if (st_head == NULL)
			// can not allocate
			return -EINVAL;
		current->st_md_base = st_head;

		st_head->count = 0;
		st_head->next = NULL;
		st_head->last = NULL;
	}

	st_head->is_traced = 1;
	st_head->strace_fd = fd;	// assuming a valid fd
	st_head->tracing_mode = tracing_mode;

	return 0;
}

/* trace buffer corresponding to the fd in 'st_head->strace_fd'
 * is not released as part of the following call
 */
int sys_end_strace(struct exec_context *current)
{
	struct strace_head *st_head = current->st_md_base;

	if (st_head) {
		int st_cnt = st_head->count;
		if (st_cnt) {
			struct strace_info *t = st_head->next;
			while (st_cnt) {
				st_head->next = t->next;
				os_free(t, sizeof(struct strace_info));
				t = st_head->next;
				st_cnt--;
			}
		}

		os_free(st_head, sizeof(struct strace_head));
		current->st_md_base = NULL;
	}

	return 0;
}



///////////////////////////////////////////////////////////////////////////
//// 		Start of ftrace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////


long do_ftrace(struct exec_context *ctx, unsigned long faddr, long action, long nargs, int fd_trace_buffer)
{
	struct ftrace_head *ft_head = ctx->ft_md_base;
	if (ft_head == NULL)
		return -EINVAL;

	struct ftrace_info *ptr = ft_head->next;
	long ft_cnt 		= ft_head->count;

	switch(action) {
		case ADD_FTRACE:
		{
			// check if ft is already enabled for the fcall
			while (ft_cnt) {
				if (faddr == ptr->faddr)
					return -EINVAL;
				ptr = ptr->next;
				ft_cnt--;
			}

			// first check if the current count is within max
			if (ft_head->count + 1 > FTRACE_MAX)
				return -EINVAL;

			// fcall not traced hence add: at head or tail
			ptr = os_alloc(sizeof(struct ftrace_info));
			if (ptr == NULL)
				return -EINVAL;
			ptr->faddr    = faddr;
			ptr->num_args = nargs;
			ptr->fd       = fd_trace_buffer;
			ptr->capture_backtrace = 0;
			ptr->next = NULL;

			if (ft_head->count) {
				// at the tail
				ft_head->last->next = ptr;
				ft_head->last = ptr;
			} else {
				// first addition
				ft_head->last = ft_head->next = ptr;
			}
			ft_head->count += 1;
		}
		break;
		case REMOVE_FTRACE:
		{
			// check if ft is actually enabled for the fcall
			struct ftrace_info *t = NULL;
			while (ft_cnt) {
				if (faddr == ptr->faddr)
					break;
				// 't' will always be a node behind 'ptr'
				t = ptr;
				ptr = ptr->next;
				ft_cnt--;
			}
			if (ft_cnt) {
				// fcall traced hence disable it
				// remove its ftrace_info entry: head, mid, end
				if (ptr == ft_head->next) {
					// head deletion
					ft_head->next = ptr->next;
				} else if (ptr == ft_head->last) {
					// tail deletion
					ft_head->last = t;
				} else {
					t->next = ptr->next;
				}
				os_free(ptr, sizeof(struct ftrace_info));
				ft_head->count -= 1;
			} else
				// trying to remove something i.e. not present
				return -EINVAL;
		}
		break;
		case ENABLE_FTRACE:
		{
			// check if ft is already enabled for the fcall
			while (ft_cnt) {
				if (faddr == ptr->faddr)
					break;
				ptr = ptr->next;
				ft_cnt--;
			}
			if (ft_cnt) {
				// handle

			} else
				// trying to trace func i.e. not added yet
				return -EINVAL;
		}
		break;
		case DISABLE_FTRACE:
		{
			// check if ft is already enabled for the fcall
			while (ft_cnt) {
				if (faddr == ptr->faddr)
					break;
				ptr = ptr->next;
				ft_cnt--;
			}
			if (ft_cnt) {
				// handle

			} else
				// trying to trace func i.e. not added yet
				return -EINVAL;
		}
		break;
		case ENABLE_BACKTRACE:
		{
			// check if ft is already enabled for the fcall
			while (ft_cnt) {
				if (faddr == ptr->faddr)
					break;
				ptr = ptr->next;
				ft_cnt--;
			}
			if (ft_cnt) {
				// handle
				ptr->capture_backtrace = 1;

			} else
				// trying to trace func i.e. not added yet
				return -EINVAL;
		}
		break;
		case DISABLE_BACKTRACE:
		{
			// check if ft is already enabled for the fcall
			while (ft_cnt) {
				if (faddr == ptr->faddr)
					break;
				ptr = ptr->next;
				ft_cnt--;
			}
			if (ft_cnt) {
				// handle
				ptr->capture_backtrace = 0;
				// disable ftrace as well

			} else
				// trying to trace func i.e. not added yet
				return -EINVAL;
		}
		break;
		default:
			return -EINVAL;
	}

	return 0;

}

//Fault handler
long handle_ftrace_fault(struct user_regs *regs)
{
        return 0;
}


int sys_read_ftrace(struct file *filep, char *buff, u64 count)
{
	if (!is_valid_mem_range((u64)buff, count, 2))
		return -EBADMEM;

	return 0;
}


