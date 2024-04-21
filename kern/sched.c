#include <env.h>
#include <pmap.h>
#include <printk.h>

/* Overview:
 *   Implement a round-robin scheduling to select a runnable env and schedule it using 'env_run'.
 *
 * Post-Condition:
 *   If 'yield' is set (non-zero), 'curenv' should not be scheduled again unless it is the only
 *   runnable env.
 *
 * Hints:
 *   1. The variable 'count' used for counting slices should be defined as 'static'.
 *   2. Use variable 'env_sched_list', which contains and only contains all runnable envs.
 *   3. You shouldn't use any 'return' statement because this function is 'noreturn'.
 */
// 参数表示是否强制让出当前进程的运行
// - yield为1时：此时当前进程必须让出
// - count减为0时：此时分给进程的时间片被用完，将执行权让给其他进程
// - 无当前进程：这必然是内核刚刚完成初始化，第一次产生时钟中断的情况，需要分配一个进程执行
// - 进程状态不是可运行：当前进程不能再继续执行，让给其他进程
void schedule(int yield) {
  // 时间片长度被量化为 N*TIMER_INTERVAL。
  // N就是进程的优先级，通过count追踪当前进程还可以执行的时间片长度
  static int count = 0;
  struct Env *env = curenv;

  /* We always decrease the 'count' by 1.
   *
    * If 'yield' is set,
    * or 'count' has been decreased to 0,
    * or 'e' (previous 'curenv') is 'NULL',
    * or 'e' is not runnable,
   * then we pick up a new env from 'env_sched_list' (list of
   * all runnable envs), set 'count' to its priority, and schedule it with 'env_run'.
   * **Panic if that list is empty**.
   *
   * (Note that if 'e' is still a runnable env, we should move it to the tail of
   * 'env_sched_list' before picking up another env from its head, or we will schedule the
   * head env repeatedly.)
   *
   * Otherwise, we simply schedule 'e' again.
   *
   * You may want to use macros below:
   *   'TAILQ_FIRST', 'TAILQ_REMOVE', 'TAILQ_INSERT_TAIL'
   */
  // 是否需要切换进程
  if (yield ||      // 强制切换：通过参数
      count<=0 ||   // 当前进程分配时间片结束
      env==NULL ||  // 当前无进程：刚初始化，切换一次进行分配
      env->env_status!=ENV_RUNNABLE // 当前进程被阻塞
  ) {
    if (env!=NULL) {
      // 从调度队列中移除当前进程
      TAILQ_REMOVE(&env_sched_list, env, env_sched_link);
      // 如果之前的进程还是可运行的时，需要将其插入调度队列队尾，等待下一次轮到其执行
      if (env->env_status == ENV_RUNNABLE) {
        TAILQ_INSERT_TAIL(&env_sched_list, env, env_sched_link);
      }
    }
    // 当调度队列为空时，内核崩溃，因为操作系统中必须至少有一个进程
    if (TAILQ_EMPTY(&env_sched_list)) {
      panic("schedule: no runnable envs");
    }
    // 不要在这里使用 TAILQ_REMOVE
    env = TAILQ_FIRST(&env_sched_list);
    // 将剩余时间片更新为新的进程的优先级
    count = env->env_pri;
  }
  // 无论是否进行切换，都将剩余时间片长度count减去1
  // 然后调用env_run函数，继续运行当前进程curenv
  count--;
  env_run(env);
}
