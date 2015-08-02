#ifndef PID_QUEUE
#define PID_QUEUE

#include <stdbool.h>
#include <linux/slab.h> /* kalloc/kfree */

typedef struct list_node {                        //list_node struct
    pid_t pid;
    struct list_node* next;
    bool visited;
} vec_node;

vec_node* list_init (pid_t p)
{
    vec_node *new_node = kmalloc(sizeof(vec_node), GFP_ATOMIC);
    new_node->pid = p;
    new_node->next = NULL;
    new_node->visited = false;
    
    return new_node;
}

/**
 * Adds an element to the end of a list and returns a pointer
 * to the head. If the head argument is null, the new element
 * is returned.
 */
vec_node* list_add_to_back (vec_node *head, pid_t p)
{
    vec_node *current_node = head;
    vec_node *new_node = list_init(p);
    
    if(current_node == NULL)
        return new_node;
    
    while(current_node->next != NULL)
        current_node = current_node->next;
    
    current_node->next = new_node;
    
    return head;
}

/**
 * Creates a new element and set's its next pointer to the
 * specified head. Returns a pointer to the new element
 */
vec_node* list_add_to_front (vec_node *head, pid_t p)
{
    vec_node *new_node = list_init(p);
    new_node->next = head;
    
    return new_node;
}

/**
 * If an element is found with the specified pid value
 * that element is removed from the list and the list's head
 * (unchanged or new head) is returned
 */
vec_node* list_remove_element (vec_node *head, pid_t p)
{
    vec_node *current_node, *last_node;
    
    if(head == NULL)
        return NULL;
    
    if(head->pid == p)
    {
        current_node = head;
        head = head->next;
        kfree(current_node);
        return head;
    }
    
    current_node = last_node = head;
    while((current_node = current_node->next) != NULL)
    {
        if(current_node->pid == p)
        {
            last_node->next = current_node->next;
            kfree(current_node);
            return head;
        }
        
        last_node = current_node;
    }
    
    // Element not found
    return head;
}

/**
 * Returns 0 if the element is not found and 1 if it is
 */
vec_node* list_contains (vec_node *head, pid_t p)
{
    if(head == NULL)
        return NULL;
    
    do
    {
        if(head->pid == p)
            return head;
        
    } while ((head = head->next) != NULL);
    
    return NULL;
}

void list_free_all (vec_node *head)
{
    while (head != NULL)
        head = list_remove_element(head, head->pid);
}

void list_mark_visited (vec_node *head, bool status)
{
    while(head != NULL)
    {
        head->visited = status;
        head = head->next;
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
        q->head = q->tail = list_add_to_back(NULL, p);
    else
        q->tail = list_add_to_back(q->tail, p)->next;
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
    list_free_all(q->head);
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
