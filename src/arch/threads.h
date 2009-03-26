/*
 *  Wrappers for thread/mutex/conditional variables
 *  Copyright (C) 2008 Andreas Öman
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HTSTHREADS_H__
#define HTSTHREADS_H__

#include "config.h"

#ifdef CONFIG_LIBPTHREAD

#include <pthread.h>

/**
 * Mutexes
 */
typedef pthread_mutex_t hts_mutex_t;

#define hts_mutex_init(m)            pthread_mutex_init((m), NULL)
#define hts_mutex_lock(m)            pthread_mutex_lock(m)
#define hts_mutex_unlock(m)          pthread_mutex_unlock(m)
#define hts_mutex_destroy(m)         pthread_mutex_destroy(m)

/**
 * Condition variables
 */
typedef pthread_cond_t hts_cond_t;
#define hts_cond_init(c)               pthread_cond_init((c), NULL)
#define hts_cond_signal(c)             pthread_cond_signal(c)
#define hts_cond_broadcast(c)          pthread_cond_broadcast(c)
#define hts_cond_wait(c, m)            pthread_cond_wait(c, m)
#define hts_cond_wait_timeout(c, m, t) pthread_cond_timedwait(c, m, t)
#define hts_cond_destroy(c)            pthread_cond_destroy(c)

/**
 * Threads
 */
typedef pthread_t hts_thread_t;

#define hts_thread_create_detached(f, a)  do {		        \
 pthread_t id;                                                  \
 pthread_attr_t attr;						\
 pthread_attr_init(&attr);					\
 pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);	\
 pthread_create(&id, &attr, f, a);				\
 pthread_attr_destroy(&attr);					\
} while(0)

#define hts_thread_create_joinable(t, f, a) pthread_create(t, NULL, f, a)
#define hts_thread_join(t)                  pthread_join(*(t), NULL)

#elif CONFIG_LIBOGC


/**
 * libogc threads
 */

/**
 * Mutexes
 */

#include <ogc/mutex.h>
typedef mutex_t hts_mutex_t;

#define hts_mutex_init(m)     LWP_MutexInit((m), 0)
#define hts_mutex_lock(m)     LWP_MutexLock(*(m))
#define hts_mutex_unlock(m)   LWP_MutexUnlock(*(m))
#define hts_mutex_destroy(m)  LWP_MutexDestroy(*(m))


/**
 * Condition variables
 */
#include <ogc/cond.h>
typedef cond_t hts_cond_t;

#define hts_cond_init(c)               LWP_CondInit(c)
#define hts_cond_signal(c)             LWP_CondSignal(*(c))
#define hts_cond_broadcast(c)          LWP_CondBroadcast(*(c))
#define hts_cond_wait(c, m)            LWP_CondWait(*(c), *(m))
#define hts_cond_wait_timeout(c, m, t) LWP_CondWait(*(c), *(m))
#define hts_cond_destroy(c)            LWP_CondDestroy(*(c))

/**
 * Threads
 */
#include <ogc/lwp.h>

typedef lwp_t hts_thread_t;

#define hts_thread_create_detached(f, a) do {	\
  lwp_t id;					\
  LWP_CreateThread(&id, f, a, NULL, 65535, 80);	\
} while(0);

#define hts_thread_create_joinable(h, f, a) \
  LWP_CreateThread(h, f, a, NULL, 65536, 80)

#define hts_thread_join(t)       LWP_JoinThread(*(t), NULL)


#else

#error No threading support

#endif


#endif /* HTSTHREADS_H__ */