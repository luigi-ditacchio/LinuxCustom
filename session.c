#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/types.h>
#include <linux/sched.h>

#define MAX_SIZE 16384

struct session {
	ssize_t (*old_read) (struct file *, char __user *, size_t, loff_t *);
	ssize_t (*old_write)(struct file *, const char *, size_t, loff_t *);
	int (*old_flush)(struct file *, fl_owner_t);
	spinlock_t lock;
	void *buffer;
	int size;
};

/*
 * FUNCTIONS DECLARATION
 */
void open_session(struct file *);
ssize_t read_session(struct file *, char *, size_t, loff_t *);
ssize_t write_session(struct file *, const char *, size_t, loff_t *);
int flush_session(struct file *, fl_owner_t);


void open_session(struct file *file)
{
	struct file_operations *file_op;
	
			printk(KERN_DEBUG "Process #%d : Inside open_session\n", current->pid);
	/*
	 * allocate memory for struct session
	 */
	file->f_session = kmalloc(sizeof(struct session), GFP_KERNEL);
	
	file->f_pos = 0; // just to be sure - should be 0 even with O_APPEND
	
	/*
	 * initialize fields of struct session
	 */
	file->f_session->old_read = file->f_op->read;
	file->f_session->old_write = file->f_op->write;
	file->f_session->old_flush = file->f_op->flush;
	spin_lock_init(&file->f_session->lock);
	file->f_session->buffer = kmalloc(MAX_SIZE, GFP_KERNEL); // sets the buffer where the file is copied
	file->f_session->size = kernel_read(file, file->f_pos, file->f_session->buffer, MAX_SIZE);
	
			printk(KERN_DEBUG "Process #%d : Just read file of %d bytes\n",
			current->pid, file->f_session->size);
	
	/*
	 * set the file pointer
	 */
	if (file->f_flags & O_APPEND) {
		file->f_pos = file->f_session->size;
		printk(KERN_DEBUG "Process #%d : File pointer moved at the end\n", current->pid);
		file->f_flags &= !O_APPEND;
	}
			
			printk(KERN_DEBUG "Process #%d : File operations struct before change at %p\n",
			current->pid, file->f_op);		
	/*
	 * copy file operations in new structure
	 * change read, write and flush
	 * then make f_op point to that structure
	 */
	file_op = kmalloc(sizeof(struct file_operations), GFP_KERNEL);
	*file_op = *file->f_op;
	
	file_op->read = read_session;
	file_op->write = write_session;
	file_op->flush = flush_session;
	
	file->f_op = file_op;
	
			printk(KERN_DEBUG "Process #%d : File operations struct after change at %p\n",
			current->pid, file->f_op);
			printk(KERN_DEBUG "Process #%d : Session now opened\n", current->pid);
}


ssize_t read_session(struct file *file, char __user *buf, size_t len, loff_t *off)
{			
	struct session *session = file->f_session;
	ssize_t ret;
	
			printk(KERN_DEBUG "Process #%d : Inside read_session\n", current->pid);
	
	spin_lock(&session->lock);
	
	ret = (session->size)-(*off);
	
	if (len < ret ) {
		ret = len;
	}
	copy_to_user( buf, (session->buffer)+(*off), ret );
	*off += ret;
	
	spin_unlock(&session->lock);
	
			printk(KERN_DEBUG "Process #%d : Read %d bytes from session\n", current->pid, (int)ret);
			
	return ret;
}

ssize_t write_session(struct file *file, const char *buf, size_t len, loff_t *off)
{
	struct session *session = file->f_session;
	ssize_t ret;
	
			printk(KERN_DEBUG "Process #%d : Inside write_session\n", current->pid);
			
	spin_lock(&session->lock);
	
	ret = (MAX_SIZE)-(*off);
	
	if (len < ret ) {
		ret = len;
	}
	
	copy_from_user( (session->buffer)+(*off) , buf, ret );
	*off += ret;
	if (*off > session->size) {
		session->size = *off;
	}
	
			printk(KERN_DEBUG "Process #%d : Written %d bytes on session\n", current->pid, (int)ret);
			
	spin_unlock(&session->lock);		
	
	return ret;

}

int flush_session(struct file *file, fl_owner_t id)
{
	struct session *session = file->f_session;
	int written, ret = 0;
	
			printk(KERN_DEBUG "Process #%d : Inside flush_session\n", current->pid);
			
	spin_lock(&session->lock);
	
	/*
	 * restore previous file operations
	 * read is not really needed
	 */
	file->f_op->read = session->old_read;
	file->f_op->write = session->old_write;
	file->f_op->flush = session->old_flush;
	
	
	/*
	 * write content of session buffer
	 * on actual file
	 */
	file->f_pos = 0;
	written = (int)kernel_write(file, session->buffer, session->size, file->f_pos);
	
			printk(KERN_DEBUG "Process #%d : Just written %d bytes on file - on the flush\n",
			current->pid, written);
	
	
	spin_unlock(&session->lock);
			
	/*
	 *flush if needed
	 */
	if (file->f_op->flush)
		ret = file->f_op->flush(file, id);
	
	/*
	 * free session area allocated with kmalloc
	 * and file operations
	 */
	kfree(session->buffer);
	kfree(session);
	kfree(file->f_op);
	
			printk(KERN_DEBUG "Process #%d : Getting out of flush_session\n",
			current->pid);
		
	return ret;
	
}
