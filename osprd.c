#include <linux/version.h>
#include <linux/autoconf.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/sched.h>
#include <linux/kernel.h>  /* printk() */
#include <linux/errno.h>   /* error codes */
#include <linux/types.h>   /* size_t */
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/wait.h>
#include <linux/file.h>

#include <stdbool.h>
#include <linux/slab.h>

#include "spinlock.h"
#include "osprd.h"


/* The size of an OSPRD sector. */
#define SECTOR_SIZE	512

/* This flag is added to an OSPRD file's f_flags to indicate that the file
 * is locked. */
#define F_OSPRD_LOCKED	0x80000

/* eprintk() prints messages to the console.
 * (If working on a real Linux machine, change KERN_NOTICE to KERN_ALERT or
 * KERN_EMERG so that you are sure to see the messages.  By default, the
 * kernel does not print all messages to the console.  Levels like KERN_ALERT
 * and KERN_EMERG will make sure that you will see messages.) */
#define eprintk(format, ...) printk(KERN_NOTICE format, ## __VA_ARGS__)

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("CS 111 RAM Disk");
MODULE_AUTHOR("Phil Crumm and Ivan Petkov");

#define OSPRD_MAJOR	222

/* This module parameter controls how big the disk will be.
 * You can specify module parameters when you load the module,
 * as an argument to insmod: "insmod osprd.ko nsectors=4096" */
static int nsectors = 32;
module_param(nsectors, int, 0);
static int ReleaseLLock(struct file *tmpfile);
static int RequestAcquireLock(struct file *filp);
/*********************************************************************************
 The Linklist Implementation
 
 *********************************************************************************/

typedef struct vnode {                        //vnode struct
    pid_t pid;
    struct vnode* next;
    bool visited;
} vec_node;

vec_node* Startlist (pid_t pi)
{
    vec_node *tmpNode = kmalloc(sizeof(vec_node), GFP_ATOMIC);
    tmpNode->next = NULL;
    tmpNode->visited = false;
    tmpNode->pid = pi;
    return tmpNode;
}

/**
 * Returns 0 if the element is not found and 1 if it is
 */
vec_node* SearchNode ( pid_t pi,vec_node *ptrhd)
{
    if(ptrhd == NULL)
        return NULL;
    
    do
    {
        if(ptrhd->pid == pi)
            return ptrhd;
        
    } while ((ptrhd = ptrhd->next) != NULL);
    
    return NULL;
}


vec_node* AddnodeEd (vec_node *ptrhd, pid_t pi)    //This funtion add an note into the end of the list
{
    vec_node *current_node = ptrhd;
    vec_node *new_node = Startlist(pi);
    
    if(current_node == NULL)
        return new_node;
    
    while(current_node->next != NULL)
        current_node = current_node->next;
    
    current_node->next = new_node;
    
    return ptrhd;
}

void MarkNodeVisted ( bool status,vec_node *ptrhd)
{
    while(ptrhd != NULL)
    {
        ptrhd->visited = status;
        ptrhd = ptrhd->next;
    }
}


vec_node* AddnodeFd (vec_node *ptrhd, pid_t pi)   //This funtion create a new element inthe front
{
    vec_node *tmpNode = Startlist(pi);
    tmpNode->next = ptrhd;
    
    return tmpNode;
}


vec_node* RemoveNode (vec_node *ptrhd, pid_t pi)    //This funtion remove an element from the list
{
    vec_node *CurNode;
    vec_node *EdNode;
    
    
    
    if(ptrhd->pid == pi)
    {
        CurNode = ptrhd;
        ptrhd = ptrhd->next;
        kfree(CurNode);
        return ptrhd;
    }
    
    if(ptrhd == NULL)
        return NULL;
    
    CurNode = EdNode = ptrhd;
    while((CurNode = CurNode->next) != NULL)
    {
        if(CurNode->pid == pi)
        {
            EdNode->next = CurNode->next;
            kfree(CurNode);
            return ptrhd;
        }
        
        EdNode = CurNode;
    }

    return ptrhd;
}




void FreeList (vec_node *ptrhd)
{
    while (ptrhd != NULL)
    {
        ptrhd = RemoveNode(ptrhd, ptrhd->pid);
    }
}


/* The internal representation of our device. */
typedef struct osprd_info {
    uint8_t *data;                  // The data array. Its size is
    // (nsectors * SECTOR_SIZE) bytes.
    
    osp_spinlock_t mutex;           // Mutex for synchronizing access to
    // this block device
    
    unsigned ticket_head;		// Currently running ticket for
    // the device lock
    
    unsigned ticket_tail;		// Next available ticket for
    // the device lock
    
    wait_queue_head_t blockq;       // Wait queue for tasks blocked on
    // the device lock
    
    /* HINT: You may want to add additional fields to help
     in detecting deadlock. */
    vec_node *lock_holder_l;
    vec_node *lock_waiter_l;
    
    // If a signal wakes a process in the queue, we re-queue everything for simplicity.
    int num_to_requeue;
    
    unsigned num_read_locks;
    unsigned num_write_locks;
    
    // The following elements are used internally; you don't need
    // to understand them.
    struct request_queue *queue;    // The device request queue.
    spinlock_t qlock;		// Used internally for mutual
    //   exclusion in the 'queue'.
    struct gendisk *gd;             // The generic disk.
} osprd_info_t;

#define NOSPRD 4
static osprd_info_t osprds[NOSPRD];

/**
 * Helper functions for dealing with deadlock handling and detection
 */

// Declare useful helper functions

/*
 * file2osprd(filp)
 *   Given an open file, check whether that file corresponds to an OSP ramdisk.
 *   If so, return a pointer to the ramdisk's osprd_info_t.
 *   If not, return NULL.
 */
static osprd_info_t *file2osprd(struct file *filp);

/**
 * Given a file struct, returns the drive ID of the "file"
 */
int Drriveid(osprd_info_t *d)
{
    int i=0;
    for (; i < NOSPRD; i++)
    {
        if (&osprds[i] == d)
        {
            return i;
        }
    }
    
    eprintk("Unrecognized device");
    return -1;
    
}

/*
 * for_each_open_file(task, callback, user_data)
 *   Given a task, call the function 'callback' once for each of 'task's open
 *   files.  'callback' is called as 'callback(filp, user_data)'; 'filp' is
 *   the open file, and 'user_data' is copied from for_each_open_file's third
 *   argument.
 */
static void for_each_open_file(struct task_struct *task,
                               void (*callback)(struct file *filp,
                                                osprd_info_t *user_data),
                               osprd_info_t *user_data);




/*
 * osprd_process_request(d, req)
 *   Called when the user reads or writes a sector.
 *   Should perform the read or write, as appropriate.
 */
static void osprd_process_request(osprd_info_t *d, struct request *req)
{
    size_t Offset;
    
    size_t Nobyte;
    
    
    if (!blk_fs_request(req)) {
        end_request(req, 0);
        return;
    }
    
    
    // EXERCISE: Perform the read or write request by copying data between
    // our data array and the request's buffer.
    // Hint: The 'struct request' argument tells you what kind of request
    // this is, and which sectors are being read or written.
    // Read about 'struct request' in <linux/blkdev.h>.
    // Consider the 'req->sector', 'req->current_nr_sectors', and
    // 'req->buffer' members, and the rq_data_dir() function.
    
    
    
    
    if ( req->sector >= nsectors
        || req->sector < 0)
    {
        eprintk("This is an in valid sector requested: [%lu]. max sectors: [%i]\n", (unsigned long)req->sector, nsectors);
        end_request(req, 0);
    }
    Offset = req->sector * SECTOR_SIZE;
    if(req->sector + req->current_nr_sectors > nsectors)
    {
        Nobyte = SECTOR_SIZE* (nsectors - req->sector) ;
        
        eprintk("The requested sector [%lu] with [%u] additional sectors.\n",
                (unsigned long)req->sector, req->current_nr_sectors);
        eprintk("Using [%u] additional sectors instead.\n", Nobyte / SECTOR_SIZE);
    }
    else
    {
        size_t tp=req->current_nr_sectors * SECTOR_SIZE;
        Nobyte=tp;
    }
    
    spin_lock(&d->mutex);
    
    if(rq_data_dir(req) == READ)
        memcpy(req->buffer, d->data + Offset, Nobyte);
    else
        memcpy(d->data + Offset, req->buffer, Nobyte);
    
    spin_unlock(&d->mutex);
    
    end_request(req, 1);
}


// This function is called when a /dev/osprdX file is opened.
// You aren't likely to need to change this.
static int osprd_open(struct inode *inode, struct file *filp)
{
    // Always set the O_SYNC flag. That way, we will get writes immediately
    // instead of waiting for them to get through write-back caches.
    filp->f_flags |= O_SYNC;
    return 0;
}


// This function is called when a /dev/osprdX file is finally closed.
// (If the file descriptor was dup2ed, this function is called only when the
// last copy is closed.)
static int osprd_close_last(struct inode *inode, struct file *filp)
{
    // EXERCISE: If the user closes a ramdisk file that holds
    // a lock, release the lock.  Also wake up blocked processes
    // as appropriate.
    
    ReleaseLLock(filp);
    
    return 0;
}



static int ReleaseLLock(struct file *tmpfile)             //This funtion release read or write lock for a given file.
{
    if (tmpfile) {
        osprd_info_t *d = file2osprd(tmpfile);
        int tmp_locked = tmpfile->f_flags & F_OSPRD_LOCKED;
        
        int tmp_writable = tmpfile->f_mode & FMODE_WRITE;
        
        
        if(tmp_locked)
        {
            spin_lock(&d->mutex);
            if(tmp_writable)
            {
                d->num_write_locks--;
            }
            else
            {
                d->num_read_locks--;
            }
            
            if(d->ticket_head < d->ticket_tail)
            {
                d->ticket_head++;
            }
            else if(d->ticket_head > d->ticket_tail)
            {
                d->ticket_head = 0;
            }
            d->lock_holder_l = RemoveNode(d->lock_holder_l, current->pid);
            spin_unlock(&d->mutex);
            
            tmpfile->f_flags &= ~F_OSPRD_LOCKED;
            
            wake_up_all(&d->blockq);
            return 0;
        }
    }
    
    return -EINVAL;
}



static int RequestAcquireLock(struct file *tmpfile)    //This funtion request to acquire a lock for a file that there are no locking
{
    osprd_info_t *ptr = file2osprd(tmpfile);
    
    int istmpfileWrt = tmpfile->f_mode & FMODE_WRITE;
    
    if(tmpfile->f_flags & F_OSPRD_LOCKED)
    {
        return -EDEADLK;
    }
    spin_lock(&ptr->mutex);
    
    
    if(  (istmpfileWrt && ptr->num_read_locks > 0) || ptr->num_write_locks > 0)
    {
        spin_unlock(&ptr->mutex);        //It check if the there is any write lock on the disk
        return -EBUSY;
    }
    
    if(istmpfileWrt)
        ptr->num_write_locks++;
    else
        ptr->num_read_locks++;
    
    ptr->lock_holder_l = AddnodeFd(ptr->lock_holder_l, current->pid);
    
    spin_unlock(&ptr->mutex);
    tmpfile->f_flags |= F_OSPRD_LOCKED;
    return 0;
}

static bool isDisknowlocked (int x)
{
    vec_node * flag;
    osprd_info_t *tp = &osprds[x];
    
    spin_lock(&tp->mutex);
    
    flag = SearchNode( current->pid,tp->lock_holder_l);
    spin_unlock(&tp->mutex);
    if(flag)
        return true;
    else
        return false;
}

static int DriveidforWaiter (pid_t id)
{
    int x=0;
    vec_node *elem = NULL;
    
    for(; x < NOSPRD; x++)
    {
        spin_lock(&(osprds[x].mutex));
        elem = SearchNode( id,osprds[x].lock_waiter_l);
        spin_unlock(&(osprds[x].mutex));
        
        if(elem && elem->visited == false)
        {
            elem->visited = true;
            return x;
        }
    }
    return -1;
}


/******************************************************************************
 The Queue Implementation
 
 ********************************************************************************/

typedef struct TheQueue {
    vec_node *tail;
    vec_node *head;
    
} TheQueue_t;

TheQueue_t* StartQueue (void)
{
    TheQueue_t *ptr = kmalloc(sizeof(TheQueue_t), GFP_ATOMIC);
    ptr->tail = NULL;
    ptr->head = NULL;
    
    return ptr;
}


bool isQueueEmpty (TheQueue_t *ptr)
{
    if(ptr->head == NULL)
        return true;
    else
        return false;
}

void PushQueue ( pid_t pi,TheQueue_t *tmp)
{
    if(tmp->head == NULL)
        tmp->head = tmp->tail = AddnodeEd(NULL, pi);
    else
        tmp->tail = AddnodeEd(tmp->tail, pi)->next;
}


pid_t PopQueue (TheQueue_t *ptr)
{
    pid_t num;
    vec_node *tmpNode;
    
    
    if(ptr->head == NULL)
        return -1;
    
    tmpNode = ptr->head;
    num = tmpNode->pid;
    
    if(ptr->head == ptr->tail)
    {
        ptr->head = NULL;
        ptr->tail = NULL;
    }
    else
        ptr->head = ptr->head->next;
    
    kfree(tmpNode);
    return num;
}

void FreeQueue (TheQueue_t *tmp)
{
    FreeList(tmp->head);
    tmp->head =NULL;
    tmp->tail = NULL;
}

void AddnodefromListtoQueue ( vec_node *ptrhd,TheQueue_t *tmp)
{
    
    if(  ptrhd == NULL|| tmp == NULL)
        return;
    
    do
    {
        PushQueue( ptrhd->pid,tmp);
    } while ((ptrhd = ptrhd->next) != NULL);
}

//*******************************************************************************
static bool check_deadlock (osprd_info_t *d)
{
    TheQueue_t *q;
    int i, d_id = Drriveid(d);
    bool ret = false;
    pid_t pid = -1;
    
    q = StartQueue();
    
    for(;;)
    {
        if(d_id > -1)
        {
            // Cannot lock the same disk twice
            if(isDisknowlocked(d_id))
            {
                ret = true;
                break;
            }
            
            spin_lock(&d->mutex);
            AddnodefromListtoQueue( osprds[d_id].lock_holder_l,q);
            spin_unlock(&d->mutex);
        }
        
        if(isQueueEmpty(q))
            break;
        
        pid = PopQueue(q);
        d_id = DriveidforWaiter(pid);
    }
    
    for(i = 0; i < NOSPRD; i++)
    {
        spin_lock(&(osprds[i].mutex));
        MarkNodeVisted( false,osprds[i].lock_waiter_l);
        spin_unlock(&(osprds[i].mutex));
    }
    
    FreeQueue(q);
    kfree(q);
    
    return ret;
}

/*
 * osprd_ioctl(inode, filp, cmd, arg)
 *   Called to perform an ioctl on the named file.
 */
int osprd_ioctl(struct inode *inode, struct file *filp,
                unsigned int cmd, unsigned long arg)
{
    osprd_info_t *d = file2osprd(filp);
    int r = 0;
    
    
    
    if (cmd == OSPRDIOCACQUIRE) {
        
        // EXERCISE: Lock the ramdisk.
        //
        // If *filp is open for writing (filp_writable), then attempt
        // to write-lock the ramdisk; otherwise attempt to read-lock
        // the ramdisk.
        //
        // This lock request must block using 'd->blockq' until:
        // 1) no other process holds a write lock;
        // 2) either the request is for a read lock, or no other process
        //    holds a read lock; and
        // 3) lock requests should be serviced in order, so no process
        //    that blocked earlier is still blocked waiting for the
        //    lock.
        //
        // If a process acquires a lock, mark this fact by setting
        // 'filp->f_flags |= F_OSPRD_LOCKED'.  You also need to
        // keep track of how many read and write locks are held:
        // change the 'osprd_info_t' structure to do this.
        //
        // Also wake up processes waiting on 'd->blockq' as needed.
        //
        // If the lock request would cause a deadlock, return -EDEADLK.
        // If the lock request blocks and is awoken by a signal, then
        // return -ERESTARTSYS.
        // Otherwise, if we can grant the lock request, return 0.
        
        // 'd->ticket_head' and 'd->ticket_tail' should help you
        // service lock requests in order.  These implement a ticket
        // order: 'ticket_tail' is the next ticket, and 'ticket_head'
        // is the ticket currently being served.  You should set a local
        // variable to 'd->ticket_head' and increment 'd->ticket_head'.
        // Then, block at least until 'd->ticket_tail == local_ticket'.
        // (Some of these operations are in a critical section and must
        // be protected by a spinlock; which ones?)
        
        // Used to track the current request
        unsigned local_ticket;
        
        if(check_deadlock(d))
            return -EDEADLK;
        
        while(RequestAcquireLock(filp) != 0)
        {
            spin_lock(&d->mutex);
            d->ticket_tail++;
            local_ticket = d->ticket_tail;
            d->lock_waiter_l = AddnodeFd(d->lock_waiter_l, current->pid);
            spin_unlock(&d->mutex);
            
            wait_event_interruptible(d->blockq, d->ticket_head == local_ticket || d->num_to_requeue > 0);
            
            spin_lock(&d->mutex);
            d->lock_waiter_l = RemoveNode(d->lock_waiter_l, current->pid);
            spin_unlock(&d->mutex);
            
            if(d->num_to_requeue > 0)
            {
                spin_lock(&d->mutex);
                d->num_to_requeue--;
                spin_unlock(&d->mutex);
                
                if(signal_pending(current))
                    return -ERESTARTSYS;
            }
            else if(signal_pending(current)) // This check if we were woken up by a signal
            {
                spin_lock(&d->mutex);
                d->num_to_requeue = (d->ticket_tail - d->ticket_head - 1);
                d->ticket_head = 0;
                d->ticket_tail = 0;
                wake_up_all(&d->blockq);
                spin_unlock(&d->mutex);
                
                if(d->num_to_requeue > 0)
                {
                    wait_event(d->blockq, d->num_to_requeue == 0);
                    wake_up_all(&d->blockq); // Wake everyone up again to check for other pending signals
                }
                return -ERESTARTSYS;
            }
        }
        
        r = 0;
        
    } else if (cmd == OSPRDIOCTRYACQUIRE) {
        
        // EXERCISE: ATTEMPT to lock the ramdisk.
        //
        // This is just like OSPRDIOCACQUIRE, except it should never
        // block.  If OSPRDIOCACQUIRE would block or return deadlock,
        // OSPRDIOCTRYACQUIRE should return -EBUSY.
        // Otherwise, if we can grant the lock request, return 0.
        
        r = RequestAcquireLock(filp);
        
        if(r == -EDEADLK)
            r = -EBUSY;
        
    } else if (cmd == OSPRDIOCRELEASE) {
        
        // EXERCISE: Unlock the ramdisk.
        //
        // If the file hasn't locked the ramdisk, return -EINVAL.
        // Otherwise, clear the lock from filp->f_flags, wake up
        // the wait queue, perform any additional accounting steps
        // you need, and return 0.
        
        r = ReleaseLLock(filp);
        
    } else
        r = -ENOTTY; /* unknown command */
    return r;
}


// Initialize internal fields for an osprd_info_t.

static void osprd_setup(osprd_info_t *d)
{
    /* Initialize the wait queue. */
    init_waitqueue_head(&d->blockq);
    osp_spin_lock_init(&d->mutex);
    d->ticket_head = d->ticket_tail = 0;
    /* Add code here if you add fields to osprd_info_t. */
    
    d->num_read_locks = 0;
    d->num_write_locks = 0;
    d->num_to_requeue = 0;
    
    d->lock_holder_l = NULL;
    d->lock_waiter_l = NULL;
}


/*****************************************************************************/
/*         THERE IS NO NEED TO UNDERSTAND ANY CODE BELOW THIS LINE!          */
/*                                                                           */
/*****************************************************************************/

// Process a list of requests for a osprd_info_t.
// Calls osprd_process_request for each element of the queue.

static void osprd_process_request_queue(request_queue_t *q)
{
    osprd_info_t *d = (osprd_info_t *) q->queuedata;
    struct request *req;
    
    while ((req = elv_next_request(q)) != NULL)
        osprd_process_request(d, req);
}


// Some particularly horrible stuff to get around some Linux issues:
// the Linux block device interface doesn't let a block device find out
// which file has been closed.  We need this information.

static struct file_operations osprd_blk_fops;
static int (*blkdev_release)(struct inode *, struct file *);

static int _osprd_release(struct inode *inode, struct file *filp)
{
    if (file2osprd(filp))
        osprd_close_last(inode, filp);
    return (*blkdev_release)(inode, filp);
}

static int _osprd_open(struct inode *inode, struct file *filp)
{
    if (!osprd_blk_fops.open) {
        memcpy(&osprd_blk_fops, filp->f_op, sizeof(osprd_blk_fops));
        blkdev_release = osprd_blk_fops.release;
        osprd_blk_fops.release = _osprd_release;
    }
    filp->f_op = &osprd_blk_fops;
    return osprd_open(inode, filp);
}


// The device operations structure.

static struct block_device_operations osprd_ops = {
    .owner = THIS_MODULE,
    .open = _osprd_open,
    // .release = osprd_release, // we must call our own release
    .ioctl = osprd_ioctl
};


// Given an open file, check whether that file corresponds to an OSP ramdisk.
// If so, return a pointer to the ramdisk's osprd_info_t.
// If not, return NULL.

static osprd_info_t *file2osprd(struct file *filp)
{
    if (filp) {
        struct inode *ino = filp->f_dentry->d_inode;
        if (ino->i_bdev
            && ino->i_bdev->bd_disk
            && ino->i_bdev->bd_disk->major == OSPRD_MAJOR
            && ino->i_bdev->bd_disk->fops == &osprd_ops)
            return (osprd_info_t *) ino->i_bdev->bd_disk->private_data;
    }
    return NULL;
}


// Call the function 'callback' with data 'user_data' for each of 'task's
// open files.

static void for_each_open_file(struct task_struct *task,
                               void (*callback)(struct file *filp, osprd_info_t *user_data),
                               osprd_info_t *user_data)
{
    int fd;
    task_lock(task);
    spin_lock(&task->files->file_lock);
    {
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 13)
        struct files_struct *f = task->files;
#else
        struct fdtable *f = task->files->fdt;
#endif
        for (fd = 0; fd < f->max_fds; fd++)
            if (f->fd[fd])
                (*callback)(f->fd[fd], user_data);
    }
    spin_unlock(&task->files->file_lock);
    task_unlock(task);
}


// Destroy a osprd_info_t.

static void cleanup_device(osprd_info_t *d)
{
    wake_up_all(&d->blockq);
    if (d->gd) {
        del_gendisk(d->gd);
        put_disk(d->gd);
    }
    if (d->queue)
        blk_cleanup_queue(d->queue);
    if (d->data)
        vfree(d->data);
    
    FreeList(d->lock_holder_l);
    FreeList(d->lock_waiter_l);
    d->lock_holder_l = NULL;
    d->lock_waiter_l = NULL;
}


// Initialize a osprd_info_t.

static int setup_device(osprd_info_t *d, int which)
{
    memset(d, 0, sizeof(osprd_info_t));
    
    /* Get memory to store the actual block data. */
    if (!(d->data = vmalloc(nsectors * SECTOR_SIZE)))
        return -1;
    memset(d->data, 0, nsectors * SECTOR_SIZE);
    
    /* Set up the I/O queue. */
    spin_lock_init(&d->qlock);
    if (!(d->queue = blk_init_queue(osprd_process_request_queue, &d->qlock)))
        return -1;
    blk_queue_hardsect_size(d->queue, SECTOR_SIZE);
    d->queue->queuedata = d;
    
    /* The gendisk structure. */
    if (!(d->gd = alloc_disk(1)))
        return -1;
    d->gd->major = OSPRD_MAJOR;
    d->gd->first_minor = which;
    d->gd->fops = &osprd_ops;
    d->gd->queue = d->queue;
    d->gd->private_data = d;
    snprintf(d->gd->disk_name, 32, "osprd%c", which + 'a');
    set_capacity(d->gd, nsectors);
    add_disk(d->gd);
    
    /* Call the setup function. */
    osprd_setup(d);
    
    return 0;
}

static void osprd_exit(void);


// The kernel calls this function when the module is loaded.
// It initializes the 4 osprd block devices.

static int __init osprd_init(void)
{
    int i, r;
    
    // shut up the compiler
    (void) for_each_open_file;
#ifndef osp_spin_lock
    (void) osp_spin_lock;
    (void) osp_spin_unlock;
#endif
    
    /* Register the block device name. */
    if (register_blkdev(OSPRD_MAJOR, "osprd") < 0) {
        printk(KERN_WARNING "osprd: unable to get major number\n");
        return -EBUSY;
    }
    
    /* Initialize the device structures. */
    for (i = r = 0; i < NOSPRD; i++)
        if (setup_device(&osprds[i], i) < 0)
            r = -EINVAL;
    
    if (r < 0) {
        printk(KERN_EMERG "osprd: can't set up device structures\n");
        osprd_exit();
        return -EBUSY;
    } else
        return 0;
}


// The kernel calls this function to unload the osprd module.
// It destroys the osprd devices.

static void osprd_exit(void)
{
    int i;
    for (i = 0; i < NOSPRD; i++)
        cleanup_device(&osprds[i]);
    unregister_blkdev(OSPRD_MAJOR, "osprd");
}


// Tell Linux to call those functions at init and exit time.
module_init(osprd_init);
module_exit(osprd_exit);
