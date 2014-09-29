#include <linux/syscalls.h>
#include <linux/ipc_namespace.h>
#include <linux/wait.h>
#include <asm/atomic.h>
#include "util.h"

#define bar_ids(ns)     ((ns)->ids[IPC_SEM_IDS])
#define LIMIT_IDS	128

struct barrier {
	struct kern_ipc_perm    ____cacheline_aligned_in_smp
                                bar_perm;       /* permissions .. see ipc.h */
        atomic_t		bar_sleepcount;
        int			bar_closing;
        wait_queue_head_t	bar_wait[32];
};


/*
 * FUNCTIONS DECLARATIONS
 */
static inline void bar_lock(struct barrier *);
static inline void bar_unlock(struct barrier *);
static inline void bar_rmid(struct ipc_namespace *, struct barrier *);
void bar_init_ns(struct ipc_namespace *);
void bar_exit_ns(struct ipc_namespace *);
void __init bar_init (void);
static int newbar(struct ipc_namespace *, struct ipc_params *);
static void freebar(struct ipc_namespace *, struct kern_ipc_perm *);
static inline int bar_security(struct kern_ipc_perm *, int);
static inline int bar_more_checks(struct kern_ipc_perm *, struct ipc_params *);





static inline void bar_lock(struct barrier *bar)
{
	spin_lock(&bar->bar_perm.lock);
}

static inline void bar_unlock(struct barrier *bar)
{
	spin_unlock(&bar->bar_perm.lock);
}



static inline void bar_rmid(struct ipc_namespace *ns, struct barrier *b)
{
        ipc_rmid(&bar_ids(ns), &b->bar_perm);
}



void bar_init_ns(struct ipc_namespace *ns)
{
	ipc_init_ids(&ns->ids[IPC_BAR_IDS]);
}

void bar_exit_ns(struct ipc_namespace *ns)
{
	free_ipcs(ns, &bar_ids(ns), freebar);
	idr_destroy(&ns->ids[IPC_BAR_IDS].ipcs_idr);
}

void __init bar_init (void)
{
	bar_init_ns(&init_ipc_ns);
}


static int newbar(struct ipc_namespace *ns, struct ipc_params *params)
{
        int id;
        struct barrier *bar;
        int size;
        key_t key = params->key;
        int semflg = params->flg;
        int i;


	/* 
	 * Allocation of the barrier and
	 * setting of kern_ipc_perm params
	 */
        size = sizeof (*bar);
        bar = ipc_rcu_alloc(size);
        if (!bar) {
        	printk(KERN_DEBUG "Process #%d : No space to allocate a new barrier\n", current->pid);
                return -ENOMEM;
        }
        memset(bar, 0, size);

        bar->bar_perm.mode = (semflg & S_IRWXUGO);
        bar->bar_perm.key = key;
        bar->bar_perm.security = NULL;
        
        
	/* 
	 * Missing security part
	 * Setting and check of security infos
	 */
	
	
	/* 
	 * The barrier is added to the ipc resources
	 * From this moment, it becomes a shared resource
	 */
        id = ipc_addid(&bar_ids(ns), &bar->bar_perm, LIMIT_IDS);
        if (id < 0) {
        	printk(KERN_DEBUG "Process #%d : Error in adding the barrier to the ipc system\n", current->pid);
                ipc_rcu_putref(bar);
                return id;
        }
        
        /* 
	 * The barrier is initialized
	 */
	atomic_set(&bar->bar_sleepcount, 0);
        for (i = 0; i < 32; i++) {
        	init_waitqueue_head(&bar->bar_wait[i]);
        }

        
        bar_unlock(bar);
        rcu_read_unlock();
        
        		printk(KERN_DEBUG "Process #%d : Barrier creation succeded\n", current->pid);

        return bar->bar_perm.id;
}


static void freebar(struct ipc_namespace *ns, struct kern_ipc_perm *ipcp)
{
	struct barrier *bar = container_of(ipcp, struct barrier, bar_perm);
	bar_rmid(ns, bar);
	bar_unlock(bar);
	rcu_read_unlock();
	ipc_rcu_putref(bar);
}




static inline int bar_security(struct kern_ipc_perm *ipcp, int barflg)
{
	return 0;
}

static inline int bar_more_checks(struct kern_ipc_perm *ipcp, struct ipc_params *params)
{
	return 0;
}



SYSCALL_DEFINE2(get_barrier, key_t, key, int, flags)
{	
	struct ipc_namespace *ns;
	struct ipc_ops bar_ops;
	struct ipc_params bar_params;
	
			printk(KERN_DEBUG "Process #%d : Inside get_barrier\n", current->pid);

	ns = current->nsproxy->ipc_ns;

	bar_ops.getnew = newbar;
	bar_ops.associate = bar_security;
	bar_ops.more_checks = bar_more_checks;

	bar_params.key = key;
	bar_params.flg = flags;

	return ipcget(ns, &bar_ids(ns), &bar_ops, &bar_params);
}

SYSCALL_DEFINE2(sleep_on_barrier, int, bd, int, tag)
{
        struct ipc_namespace *ns;
        struct kern_ipc_perm *ipcp;
        struct barrier *bar;

			printk(KERN_DEBUG "Process #%d : Inside sleep_on_barrier\n", current->pid);

        if (tag < 0 || tag > 31) 
                return -EINVAL;

        ns = current->nsproxy->ipc_ns;
        
        rcu_read_lock();
        
        /*
         * Get the struct barrier
         * First get kern_ipc_perm
         */
        ipcp = ipc_obtain_object_check(&bar_ids(ns), bd);


	if (IS_ERR(ipcp)) {
		printk(KERN_DEBUG "Process #%d : Error in ipc_obtain_object_check\n", current->pid);
		return -1;
	}

	bar = container_of(ipcp, struct barrier, bar_perm);
	if (IS_ERR(bar)) {
		printk(KERN_DEBUG "Process #%d : Error in container_of\n", current->pid);
		rcu_read_unlock();
		return -1;
	}
	
			printk(KERN_DEBUG "Process #%d : Got barrier pointer\n", current->pid);
	
	/*
         * Increment the sleep count
         * if not closing
         * then go to sleep
         */
	bar_lock(bar);
	if (bar->bar_closing) {
		//the barrier is closing
		bar_unlock(bar);
		rcu_read_unlock();
		return -1;
	}
	atomic_inc(&bar->bar_sleepcount);
	bar_unlock(bar);
	
			printk(KERN_DEBUG "Process #%d : Going to sleep\n", current->pid);
	
	
	interruptible_sleep_on(&(bar->bar_wait)[tag]);
	
			printk(KERN_DEBUG "Process #%d : Just woke up\n", current->pid);
	
	atomic_dec(&bar->bar_sleepcount);
	
	rcu_read_unlock();
	
        		printk(KERN_DEBUG "Process #%d : Exit sleep_on_barrier\n", current->pid);
        
	return 0;
}

SYSCALL_DEFINE2(awake_barrier, int, bd, int, tag)
{
	struct ipc_namespace *ns;
        struct kern_ipc_perm *ipcp;
        struct barrier *bar;

			printk(KERN_DEBUG "Process #%d : Inside awake barrier\n", current->pid);

        if (tag < 0 || tag > 31)
                return -EINVAL;

        ns = current->nsproxy->ipc_ns;
        
        rcu_read_lock();
        
        /*
         * Get the struct barrier
         * First get kern_ipc_perm
         */
        ipcp = ipc_obtain_object_check(&bar_ids(ns), bd);

	if (IS_ERR(ipcp)) {
		printk(KERN_DEBUG "Process #%d : Error in ipc_obtain_object_check\n", current->pid);
		return -1;
	}

	bar = container_of(ipcp, struct barrier, bar_perm);
	if (IS_ERR(bar)) {
		printk(KERN_DEBUG "Process #%d : Error in container_of\n", current->pid);
		rcu_read_unlock();
		return -1;
	}
	
			printk(KERN_DEBUG "Process #%d : Got barrier pointer\n", current->pid);
	
	/*
         * wake up processes
         * on a specific tag
         */
	bar_lock(bar);
	wake_up(&(bar->bar_wait)[tag]);
	
			printk(KERN_DEBUG "Process #%d : All processes on tag woke up\n", current->pid);

	bar_unlock(bar);
	rcu_read_unlock();
	
			printk(KERN_DEBUG "Process #%d : Exit awake_barrier\n", current->pid);
	
	return 0;
}

SYSCALL_DEFINE1(release_barrier, int, bd)
{	
	struct ipc_namespace *ns;
	struct kern_ipc_perm *ipcp;
	struct ipc64_perm ipc64;
	struct barrier *bar;
	int i;
	
			printk(KERN_DEBUG "Process #%d : Inside release_barrier\n", current->pid);
	
	ns = current->nsproxy->ipc_ns;
	
	/*
	 * down_write(&ids->rw_mutex) and
	 * rcu_read_lock() executed
	 * inside this function
	 */
	ipcp = ipcctl_pre_down_nolock(ns, &bar_ids(ns), bd, 0,
                                      &ipc64, 0);
        if (IS_ERR(ipcp)) {
        	printk(KERN_DEBUG "Process #%d : Error in ipcctl_pre_down_nolock\n", current->pid);
        	return -1;
        }
        
        bar = container_of(ipcp, struct barrier, bar_perm);
        
        /*
         * lock kern_ipc_perm
         * removed from ipc resources
         */
        bar_lock(bar);
        
	bar_rmid(ns, bar);
	
	bar->bar_closing++;
	
			printk(KERN_DEBUG "Process #%d : Barrier removed from ipc resources\n", current->pid);
	
	bar_unlock(bar);
	
	while (atomic_read(&bar->bar_sleepcount)) {
		for (i = 0; i < 32; i++) {
			wake_up(&(bar->bar_wait)[i]);
		}
		schedule();
	}
	
	rcu_read_unlock();
	
			printk(KERN_DEBUG "Process #%d : All processes woke up - barrier is free now\n", current->pid);
	
	/*
	 * Barrier memory is freed
	 */
	ipc_rcu_putref(bar);
	
        up_write(&bar_ids(ns).rw_mutex);
        
        		printk(KERN_DEBUG "Process #%d : Exit release_barrier\n", current->pid);
        
        return 0;
}
