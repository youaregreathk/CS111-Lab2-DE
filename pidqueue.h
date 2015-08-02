#ifndef PID_QUEUE
#define PID_QUEUE

#include <stdbool.h>
#include <linux/slab.h>

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
 * Adds an element to the end of a list and returns a pointer
 * to the head. If the head argument is null, the new element
 * is returned.
 */
vec_node* AddnodeEd (vec_node *ptrhd, pid_t pi)
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

/**
 * Creates a new element and set's its next pointer to the
 * specified head. Returns a pointer to the new element
 */
vec_node* AddnodeFd (vec_node *ptrhd, pid_t pi)
{
    vec_node *tmpNode = Startlist(pi);
    tmpNode->next = ptrhd;
    
    return tmpNode;
}

/**
 * If an element is found with the specified pid value
 * that element is removed from the list and the list's head
 * (unchanged or new head) is returned
 */
vec_node* RemoveNode (vec_node *ptrhd, pid_t pi)
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
    
    // Element not found
    return ptrhd;
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


void FreeList (vec_node *ptrhd)
{
    while (ptrhd != NULL)
    {
        ptrhd = RemoveNode(ptrhd, ptrhd->pid);
    }
}

void MarkNodeVisted ( bool status,vec_node *ptrhd)
{
    while(ptrhd != NULL)
    {
        ptrhd->visited = status;
        ptrhd = ptrhd->next;
    }
}
//End of Linklist.h
//************************************************************************************
typedef struct pid_queue {
    vec_node *head;
    vec_node *tail;
} pid_queue_t;

pid_queue_t* pid_queue_init (void)
{
    pid_queue_t *q = kmalloc(sizeof(pid_queue_t), GFP_ATOMIC);
    q->head = NULL;
    q->tail = NULL;
    
    return q;
}

/* Returns 1 if the queue has elements, 0 if empty */
bool pid_queue_empty (pid_queue_t *q)
{
    if(q->head == NULL)
        return true;
    
    return false;
}

void pid_queue_push (pid_queue_t *q, pid_t p)
{
    if(q->head == NULL)
        q->head = q->tail = AddnodeEd(NULL, p);
    else
        q->tail = AddnodeEd(q->tail, p)->next;
}

/**
 * Pops an element from the queue.
 */
pid_t pid_queue_pop (pid_queue_t *q)
{
    vec_node *elem;
    pid_t ret;
    
    if(q->head == NULL)
        return -1;
    
    elem = q->head;
    ret = elem->pid;
    
    if(q->head == q->tail)
        q->head = q->tail = NULL;
    else
        q->head = q->head->next;
    
    kfree(elem);
    return ret;
}

void pid_queue_remove_all (pid_queue_t *q)
{
    FreeList(q->head);
    q->head = q->tail = NULL;
}

void pid_queue_add_elements_from_list (pid_queue_t *q, vec_node *head)
{
    
    if(q == NULL || head == NULL)
        return;
    
    do
    {
        pid_queue_push(q, head->pid);
    } while ((head = head->next) != NULL);
}
#endif // PID_QUEUE
