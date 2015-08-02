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

void MarkNodeVisted ( bool status,vec_node *ptrhd)
{
    while(ptrhd != NULL)
    {
        ptrhd->visited = status;
        ptrhd = ptrhd->next;
    }
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




void FreeList (vec_node *ptrhd)
{
    while (ptrhd != NULL)
    {
        ptrhd = RemoveNode(ptrhd, ptrhd->pid);
    }
}


//End of Linklist.h
//************************************************************************************
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

/* Returns 1 if the queue has elements, 0 if empty */
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

/**
 * Pops an element from the queue.
 */
pid_t PopQueue (TheQueue_t *ptr)
{
    
    vec_node *tmpNode;
    
    
    if(ptr->head == NULL)
        return -1;
    
    tmpNode = ptr->head;
    ret = elem->pid;
    
    if(ptr->head == ptr->tail)
    {
        ptr->head = NULL;
        ptr->tail = NULL;
    }
    else
        ptr->head = ptr->head->next;
    
    kfree(tmpNode);
    return ret;
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
#endif // PID_QUEUE
