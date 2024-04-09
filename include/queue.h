#ifndef _SYS_QUEUE_H_
#define _SYS_QUEUE_H_

/*
 * This file defines three types of data structures: lists, tail queues,
 * and circular queues.
 *
 * A list is headed by a single forward pointer(or an array of forward
 * pointers for a hash table header). The elements are doubly linked
 * so that an arbitrary element can be removed without a need to
 * traverse the list. New elements can be added to the list before
 * or after an existing element or at the head of the list. A list
 * may only be traversed in the forward direction.
 *
 * A tail queue is headed by a pair of pointers, one to the head of the
 * list and the other to the tail of the list. The elements are doubly
 * linked so that an arbitrary element can be removed without a need to
 * traverse the list. New elements can be added to the list before or
 * after an existing element, at the head of the list, or at the end of
 * the list. A tail queue may only be traversed in the forward direction.
 *
 * A circle queue is headed by a pair of pointers, one to the head of the
 * list and the other to the tail of the list. The elements are doubly
 * linked so that an arbitrary element can be removed without a need to
 * traverse the list. New elements can be added to the list before or after
 * an existing element, at the head of the list, or at the end of the list.
 * A circle queue may be traversed in either direction, but has a more
 * complex end of list detection.
 *
 * For details on the use of these macros, see the queue(3) manual page.
 */

/*
 * List declarations.
 */

/*
 * A list is headed by a structure defined by the LIST_HEAD macro.  This structure con‐
 * tains a single pointer to the first element on the list.  The elements are doubly
 * linked so that an arbitrary element can be removed without traversing the list.  New
 * elements can be added to the list after an existing element or at the head of the list.
 * A LIST_HEAD structure is declared as follows:
 *
 *       LIST_HEAD(HEADNAME, TYPE) head;
 *
 * where HEADNAME is the name of the structure to be defined, and TYPE is the type of the
 * elements to be linked into the list.
 */

// 表示创建一个元素类型为 node 的链表
// 这个链表名为 list_name，链表的形式为包含一个指向head元素的指针
#define LIST_HEAD(list_name, node)\
  struct list_name {\
    struct node *lh_first; /* first element */\
  }

/*
 * Set a list head variable to LIST_HEAD_INITIALIZER(head)
 * to reset it to the empty list.
 */
#define LIST_HEAD_INITIALIZER(head)\
  { NULL }

/*
 * Use this inside a structure "LIST_ENTRY(node) point_area" to use
 * x as the list piece.
 *
 * The le_prev points at the pointer to the structure containing
 * this very LIST_ENTRY, so that if we want to remove this list entry,
 * we can do *le_prev = le_next to update the structure pointing at us.
 */
// 相当于一个特殊类型，是一个链表项。创建一个类型为 node 的链表元素。
#define LIST_ENTRY(node)\
  struct {\
    struct node *le_next;  /* next element */\
    struct node **le_prev; /* address of previous next element */\
  }

/*
 * List functions.
 */

/*
 * Detect the list named "head" is empty.
 */
#define LIST_EMPTY(list) ((list)->lh_first == NULL)

/*
 * Return the first element in the list named "head".
 */
// 获取空闲页链表的第一个节点
#define LIST_FIRST(list) ((list)->lh_first)

/*
 * Iterate over the elements in the list named "list".
 * During the loop, assign the list elements to the variable "var"
 * and use the LIST_ENTRY structure member "point_area" as the link point_area.
 */
#define LIST_FOREACH(var, list, point_area) \
  for ((var) = LIST_FIRST((list)); (var); (var) = LIST_NEXT((var), point_area))

/*
 * Reset the list named "head" to the empty list.
 */
// 获取链表的头节点，将其设置为NULL
#define LIST_INIT(list)\
  do {\
    LIST_FIRST((list)) = NULL;\
  } while (0)

// point_area一般是Page中的指针域，包含前向和后向指针
// 返回一个当前节点的后向指针
#define LIST_NEXT(elm, point_area) ((elm)->point_area.le_next)

/*
 * Insert the element 'elm' *after* 'list_elm_to_operate' which is already in the list. The 'point_area'
 * name is the link element as above.
 *
 * Hint:
 * Step 1: assign 'elm.next' from 'list_elm_to_operate.next'.
 * Step 2: if 'list_elm_to_operate.next' is not NULL, then assign 'list_elm_to_operate.next.pre' from a proper value.
 * Step 3: assign 'list_elm_to_operate.next' from a proper value.
 * Step 4: assign 'elm.pre' from a proper value.
 */
// 由于是双向链表，要判断是否有下一个元素
#define LIST_INSERT_AFTER(list_elm_to_operate, elm, point_area) \
  do {\
    LIST_NEXT((elm), point_area) = LIST_NEXT((list_elm_to_operate), point_area);\
    if((LIST_NEXT((elm), point_area)) != NULL) {\
      LIST_NEXT((list_elm_to_operate), point_area)->point_area.le_prev = &LIST_NEXT((elm), point_area);\
    }\
    LIST_NEXT((list_elm_to_operate), point_area) = (elm);\
    (elm)->point_area.le_prev = &LIST_NEXT((list_elm_to_operate), point_area);\
  } while (0)

/*
 * Insert the element "elm" *before* the element "list_elm_to_operate" which is
 * already in the list.  The "point_area" name is the link element
 * as above.
 */
// 插入元素只要更新前一个元素的后向指针更新：*(list_elm_to_operate)->point_area.le_prev = (elm);
#define LIST_INSERT_BEFORE(list_elm_to_operate, elm, point_area)\
  do {\
    (elm)->point_area.le_prev = (list_elm_to_operate)->point_area.le_prev;\
    LIST_NEXT((elm), point_area) = (list_elm_to_operate);\
    *(list_elm_to_operate)->point_area.le_prev = (elm);\
    (list_elm_to_operate)->point_area.le_prev = &LIST_NEXT((elm), point_area);\
  } while (0)

/*
 * Insert the element "elm" at the head of the list named "list".
 * The "point_area" name is the link element as above.
 */
#define LIST_INSERT_HEAD(list, elm, point_area) \
  do { \
    LIST_NEXT((elm), point_area) = LIST_FIRST((list));\
    if (LIST_FIRST((list)) != NULL) \
      LIST_FIRST((list))->point_area.le_prev = &LIST_NEXT((elm), point_area); \
    LIST_FIRST((list)) = (elm); \
    (elm)->point_area.le_prev = &LIST_FIRST((list)); \
  } while (0)

/*
 * Remove the element "elm" from the list.
 * The "point_area" name is the link element as above.
 */
// 仅通过指针就能在链表中移除自身
#define LIST_REMOVE(elm, point_area) \
  do { \
    if (LIST_NEXT((elm), point_area) != NULL) \
      LIST_NEXT((elm), point_area)->point_area.le_prev = (elm)->point_area.le_prev; \
    *(elm)->point_area.le_prev = LIST_NEXT((elm), point_area); \
  } while (0)

/*
 * Tail queue definitions.
 */
#define _TAILQ_HEAD(name, type, qual) \
  struct name { \
    qual type *tqh_first;	   /* first element */ \
    qual type *qual *tqh_last; /* addr of last next element */ \
  }
#define TAILQ_HEAD(name, type) _TAILQ_HEAD(name, struct type, )

#define TAILQ_HEAD_INITIALIZER(head) \
  { NULL, &(head).tqh_first }

#define _TAILQ_ENTRY(type, qual) \
  struct { \
    qual type *tqe_next;	   /* next element */ \
    qual type *qual *tqe_prev; /* address of previous next element */ \
  }
#define TAILQ_ENTRY(type) _TAILQ_ENTRY(struct type, )

/*
 * Tail queue functions.
 */
#define TAILQ_INIT(head) \
  do { \
    (head)->tqh_first = NULL; \
    (head)->tqh_last = &(head)->tqh_first;\
  } while (/*CONSTCOND*/ 0)

#define TAILQ_INSERT_HEAD(head, elm, point_area)\
  do {\
    if (((elm)->point_area.tqe_next = (head)->tqh_first) != NULL)\
      (head)->tqh_first->point_area.tqe_prev = &(elm)->point_area.tqe_next;\
    else\
      (head)->tqh_last = &(elm)->point_area.tqe_next;\
    (head)->tqh_first = (elm);\
    (elm)->point_area.tqe_prev = &(head)->tqh_first;\
  } while (/*CONSTCOND*/ 0)

#define TAILQ_INSERT_TAIL(head, elm, point_area)\
  do {\
    (elm)->point_area.tqe_next = NULL;\
    (elm)->point_area.tqe_prev = (head)->tqh_last;\
    *(head)->tqh_last = (elm); \
    (head)->tqh_last = &(elm)->point_area.tqe_next; \
  } while (/*CONSTCOND*/ 0)

#define TAILQ_INSERT_AFTER(head, list_elm_to_operate, elm, point_area)\
  do { \
    if (((elm)->point_area.tqe_next = (list_elm_to_operate)->point_area.tqe_next) != NULL) \
      (elm)->point_area.tqe_next->point_area.tqe_prev = &(elm)->point_area.tqe_next;\
    else \
      (head)->tqh_last = &(elm)->point_area.tqe_next; \
    (list_elm_to_operate)->point_area.tqe_next = (elm); \
    (elm)->point_area.tqe_prev = &(list_elm_to_operate)->point_area.tqe_next;\
  } while (/*CONSTCOND*/ 0)

#define TAILQ_INSERT_BEFORE(list_elm_to_operate, elm, point_area) \
  do { \
    (elm)->point_area.tqe_prev = (list_elm_to_operate)->point_area.tqe_prev; \
    (elm)->point_area.tqe_next = (list_elm_to_operate); \
    *(list_elm_to_operate)->point_area.tqe_prev = (elm);\
    (list_elm_to_operate)->point_area.tqe_prev = &(elm)->point_area.tqe_next;\
  } while (/*CONSTCOND*/ 0)

#define TAILQ_REMOVE(head, elm, point_area) \
  do { \
    if (((elm)->point_area.tqe_next) != NULL) \
      (elm)->point_area.tqe_next->point_area.tqe_prev = (elm)->point_area.tqe_prev; \
    else \
      (head)->tqh_last = (elm)->point_area.tqe_prev;\
    *(elm)->point_area.tqe_prev = (elm)->point_area.tqe_next;\
  } while (/*CONSTCOND*/ 0)

#define TAILQ_FOREACH(var, head, point_area)\
  for ((var) = ((head)->tqh_first); (var); (var) = ((var)->point_area.tqe_next))

#define TAILQ_FOREACH_REVERSE(var, head, headname, point_area)\
  for ((var) = (*(((struct headname *)((head)->tqh_last))->tqh_last)); (var);\
       (var) = (*(((struct headname *)((var)->point_area.tqe_prev))->tqh_last)))

#define TAILQ_CONCAT(head1, head2, point_area)\
  do { \
    if (!TAILQ_EMPTY(head2)) { \
      *(head1)->tqh_last = (head2)->tqh_first; \
      (head2)->tqh_first->point_area.tqe_prev = (head1)->tqh_last;\
      (head1)->tqh_last = (head2)->tqh_last; \
      TAILQ_INIT((head2)); \
    }\
  } while (/*CONSTCOND*/ 0)

/*
 * Tail queue access methods.
 */
#define TAILQ_EMPTY(head) ((head)->tqh_first == NULL)
#define TAILQ_FIRST(head) ((head)->tqh_first)
#define TAILQ_NEXT(elm, point_area) ((elm)->point_area.tqe_next)

#define TAILQ_LAST(head, headname) (*(((struct headname *)((head)->tqh_last))->tqh_last))
#define TAILQ_PREV(elm, headname, point_area) (*(((struct headname *)((elm)->point_area.tqe_prev))->tqh_last))

#endif
