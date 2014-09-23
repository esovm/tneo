/*

  TNKernel real-time kernel

  Copyright � 2004, 2013 Yuri Tiomkin
  All rights reserved.

  Permission to use, copy, modify, and distribute this software in source
  and binary forms and its documentation for any purpose and without fee
  is hereby granted, provided that the above copyright notice appear
  in all copies and that both that copyright notice and this permission
  notice appear in supporting documentation.

  THIS SOFTWARE IS PROVIDED BY THE YURI TIOMKIN AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL YURI TIOMKIN OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
  SUCH DAMAGE.

*/

  /* ver 2.7  */


/*******************************************************************************
 *    INCLUDED FILES
 ******************************************************************************/

//-- common tnkernel headers
#include "tn_common.h"
#include "tn_sys.h"
#include "tn_internal.h"

//-- header of current module
#include "tn_sem.h"

//-- header of other needed modules
#include "tn_tasks.h"




/*******************************************************************************
 *    PRIVATE FUNCTIONS
 ******************************************************************************/

static inline enum TN_RCode _sem_job_perform(
      struct TN_Sem *sem,
      int (p_worker)(struct TN_Sem *sem),
      unsigned long timeout
      )
{
   TN_INTSAVE_DATA;
   enum TN_RCode rc = TN_RC_OK;
   BOOL waited_for_sem = FALSE;

#if TN_CHECK_PARAM
   if(sem == NULL)
      return  TN_RC_WPARAM;
   if(sem->max_count == 0)
      return  TN_RC_WPARAM;
   if(sem->id_sem != TN_ID_SEMAPHORE)
      return TN_RC_INVALID_OBJ;
#endif

   TN_CHECK_NON_INT_CONTEXT;

   tn_disable_interrupt();

   rc = p_worker(sem);

   if (rc == TN_RC_TIMEOUT && timeout != 0){
      _tn_task_curr_to_wait_action(&(sem->wait_queue), TN_WAIT_REASON_SEM, timeout);

      //-- rc will be set later thanks to waited_for_sem
      waited_for_sem = TRUE;
   }

#if TN_DEBUG
   if (!_tn_need_context_switch() && waited_for_sem){
      TN_FATAL_ERROR("");
   }
#endif

   tn_enable_interrupt();
   _tn_switch_context_if_needed();
   if (waited_for_sem){
      rc = tn_curr_run_task->task_wait_rc;
   }

   return rc;
}

static inline enum TN_RCode _sem_job_iperform(
      struct TN_Sem *sem,
      int (p_worker)(struct TN_Sem *sem)
      )
{
   TN_INTSAVE_DATA_INT;
   enum TN_RCode rc = TN_RC_OK;

#if TN_CHECK_PARAM
   if(sem == NULL)
      return  TN_RC_WPARAM;
   if(sem->max_count == 0)
      return  TN_RC_WPARAM;
   if(sem->id_sem != TN_ID_SEMAPHORE)
      return TN_RC_INVALID_OBJ;
#endif

   TN_CHECK_INT_CONTEXT;

   tn_idisable_interrupt();

   rc = p_worker(sem);

   tn_ienable_interrupt();

   return rc;
}

static inline enum TN_RCode _sem_signal(struct TN_Sem *sem)
{
   enum TN_RCode rc = TN_RC_OK;

   if (!(tn_is_list_empty(&(sem->wait_queue)))){
      struct TN_Task *task;
      //-- there are tasks waiting for that semaphore,
      //   so, wake up first one

      //-- get first task from semaphore's wait_queue
      task = tn_list_first_entry(&(sem->wait_queue), typeof(*task), task_queue);

      //-- wake it up
      _tn_task_wait_complete(task, TN_RC_OK);
   } else {
      //-- no tasks are waiting for that semaphore,
      //   so, just increase its count if possible.
      if (sem->count < sem->max_count){
         sem->count++;
      } else {
         rc = TN_RC_OVERFLOW;
      }
   }

   return rc;
}

static inline enum TN_RCode _sem_acquire(struct TN_Sem *sem)
{
   enum TN_RCode rc = TN_RC_OK;

   if (sem->count >= 1){
      sem->count--;
   } else {
      rc = TN_RC_TIMEOUT;
   }

   return rc;
}





/*******************************************************************************
 *    PUBLIC FUNCTIONS
 ******************************************************************************/

//----------------------------------------------------------------------------
//   Structure's field sem->id_sem have to be set to 0
//----------------------------------------------------------------------------
enum TN_RCode tn_sem_create(struct TN_Sem * sem,
                  int start_count,
                  int max_count)
{

#if TN_CHECK_PARAM
   if(sem == NULL) //-- Thanks to Michael Fisher
      return  TN_RC_WPARAM;
   if(max_count <= 0 || start_count < 0 ||
         start_count > max_count || sem->id_sem != 0) //-- no recreation
   {
      sem->max_count = 0;
      return  TN_RC_WPARAM;
   }
#endif

   TN_CHECK_NON_INT_CONTEXT;

   tn_list_reset(&(sem->wait_queue));

   sem->count     = start_count;
   sem->max_count = max_count;
   sem->id_sem    = TN_ID_SEMAPHORE;

   return TN_RC_OK;
}

//----------------------------------------------------------------------------
enum TN_RCode tn_sem_delete(struct TN_Sem * sem)
{
   TN_INTSAVE_DATA;

#if TN_CHECK_PARAM
   if(sem == NULL)
      return TN_RC_WPARAM;
   if(sem->id_sem != TN_ID_SEMAPHORE)
      return TN_RC_INVALID_OBJ;
#endif

   TN_CHECK_NON_INT_CONTEXT;

   tn_disable_interrupt(); // v.2.7 - thanks to Eugene Scopal

   _tn_wait_queue_notify_deleted(&(sem->wait_queue));

   sem->id_sem = 0; //-- Semaphore does not exist now

   tn_enable_interrupt();

   //-- we might need to switch context if _tn_wait_queue_notify_deleted()
   //   has woken up some high-priority task
   _tn_switch_context_if_needed();

   return TN_RC_OK;
}

//----------------------------------------------------------------------------
//  Release Semaphore Resource
//----------------------------------------------------------------------------
enum TN_RCode tn_sem_signal(struct TN_Sem *sem)
{
   return _sem_job_perform(sem, _sem_signal, 0);
}

//----------------------------------------------------------------------------
// Release Semaphore Resource inside Interrupt
//----------------------------------------------------------------------------
enum TN_RCode tn_sem_isignal(struct TN_Sem *sem)
{
   return _sem_job_iperform(sem, _sem_signal);
}

//----------------------------------------------------------------------------
//   Acquire Semaphore Resource
//----------------------------------------------------------------------------
enum TN_RCode tn_sem_acquire(struct TN_Sem *sem, unsigned long timeout)
{
   return _sem_job_perform(sem, _sem_acquire, timeout);
}

//----------------------------------------------------------------------------
//  Acquire(Polling) Semaphore Resource (do not call  in the interrupt)
//----------------------------------------------------------------------------
enum TN_RCode tn_sem_polling(struct TN_Sem *sem)
{
   return _sem_job_perform(sem, _sem_acquire, 0);
}

//----------------------------------------------------------------------------
// Acquire(Polling) Semaphore Resource inside interrupt
//----------------------------------------------------------------------------
enum TN_RCode tn_sem_ipolling(struct TN_Sem *sem)
{
   return _sem_job_iperform(sem, _sem_acquire);
}

//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

