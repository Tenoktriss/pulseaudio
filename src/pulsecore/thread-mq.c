/***
  This file is part of PulseAudio.

  Copyright 2006 Lennart Poettering

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include <errno.h>

#include <pulsecore/thread.h>
#include <pulsecore/semaphore.h>
#include <pulsecore/macro.h>

#include "thread-mq.h"

PA_STATIC_TLS_DECLARE_NO_FREE(thread_mq);

static void asyncmsgq_read_cb(pa_mainloop_api*api, pa_io_event* e, int fd, pa_io_event_flags_t events, void *userdata) {
    pa_thread_mq *q = userdata;
    pa_asyncmsgq *aq;

    pa_assert(pa_asyncmsgq_read_fd(q->outq) == fd);
    pa_assert(events == PA_IO_EVENT_INPUT);

    pa_asyncmsgq_ref(aq = q->outq);
    pa_asyncmsgq_read_after_poll(aq);

    for (;;) {
        pa_msgobject *object;
        int code;
        void *data;
        int64_t offset;
        pa_memchunk chunk;

        /* Check whether there is a message for us to process */
        while (pa_asyncmsgq_get(aq, &object, &code, &data, &offset, &chunk, 0) >= 0) {
            int ret;

            ret = pa_asyncmsgq_dispatch(object, code, data, offset, &chunk);
            pa_asyncmsgq_done(aq, ret);
        }

        if (pa_asyncmsgq_read_before_poll(aq) == 0)
            break;
    }

    pa_asyncmsgq_unref(aq);
}

static void asyncmsgq_write_cb(pa_mainloop_api*api, pa_io_event* e, int fd, pa_io_event_flags_t events, void *userdata) {
    pa_thread_mq *q = userdata;

    pa_assert(pa_asyncmsgq_write_fd(q->inq) == fd);
    pa_assert(events == PA_IO_EVENT_INPUT);

    pa_asyncmsgq_write_after_poll(q->inq);
    pa_asyncmsgq_write_before_poll(q->inq);
}

/* used to setup mainloop io objects + mainloop callbacks */
static void pa_thread_mq_init_mainloop(pa_thread_mq *q, pa_mainloop_api *mainloop) {
    pa_assert(q);
    pa_assert(mainloop);
    q->mainloop = mainloop;

    pa_assert_se(pa_asyncmsgq_read_before_poll(q->outq) == 0);
    pa_assert_se(q->read_event = mainloop->io_new(mainloop, pa_asyncmsgq_read_fd(q->outq), PA_IO_EVENT_INPUT, asyncmsgq_read_cb, q));

    pa_asyncmsgq_write_before_poll(q->inq);
    pa_assert_se(q->write_event = mainloop->io_new(mainloop, pa_asyncmsgq_write_fd(q->inq), PA_IO_EVENT_INPUT, asyncmsgq_write_cb, q));

    q->rt_mainloop_tmq = NULL;
}

/* IO Context should be used when you use within your module also a mainloop */
void pa_thread_mq_init_rtmainloop_post(pa_thread_mq *q, pa_thread_mq *rt_q, pa_mainloop_api *rt_mainloop) {
    pa_assert(q);
    pa_assert(rt_mainloop);
    pa_assert(q->inq);
    pa_assert(q->outq);
    pa_assert(q->mainloop);
    pa_assert(!q->rt_mainloop_tmq);

    q->rt_mainloop_tmq = rt_q;
    /* swapping inq/outq */
    rt_q->inq = q->outq;
    rt_q->outq = q->inq;

    rt_q->mainloop = rt_mainloop;

    pa_assert_se(rt_q->read_event = rt_mainloop->io_new(rt_mainloop, pa_asyncmsgq_read_fd(rt_q->outq), PA_IO_EVENT_INPUT, asyncmsgq_read_cb, rt_q));
    pa_assert_se(rt_q->write_event = rt_mainloop->io_new(rt_mainloop, pa_asyncmsgq_write_fd(rt_q->inq), PA_IO_EVENT_INPUT, asyncmsgq_write_cb, rt_q));
}

void pa_thread_mq_init_rtmainloop_pre(pa_thread_mq *q, pa_mainloop_api *mainloop) {
    pa_assert(q);
    pa_assert(mainloop);

    pa_assert_se(q->inq = pa_asyncmsgq_new(0));
    pa_assert_se(q->outq = pa_asyncmsgq_new(0));

    pa_thread_mq_init_mainloop(q, mainloop);
}

/* should be used when using a rt_poll as module threading loop */
void pa_thread_mq_init(pa_thread_mq *q, pa_mainloop_api *mainloop, pa_rtpoll *rtpoll) {
    pa_assert(q);
    pa_assert(mainloop);

    pa_assert_se(q->inq = pa_asyncmsgq_new(0));
    pa_assert_se(q->outq = pa_asyncmsgq_new(0));

    pa_thread_mq_init_mainloop(q, mainloop);

    pa_rtpoll_item_new_asyncmsgq_read(rtpoll, PA_RTPOLL_EARLY, q->inq);
    pa_rtpoll_item_new_asyncmsgq_write(rtpoll, PA_RTPOLL_LATE, q->outq);
}

void pa_thread_mq_done(pa_thread_mq *q) {
    pa_assert(q);

    /* Since we are called from main context we can be sure that the
     * inq is empty. However, the outq might still contain messages
     * for the main loop, which we need to dispatch (e.g. release
     * msgs, other stuff). Hence do so if we aren't currently
     * dispatching anyway. */

    if (!pa_asyncmsgq_dispatching(q->outq))
        pa_asyncmsgq_flush(q->outq, TRUE);

    q->mainloop->io_free(q->read_event);
    q->mainloop->io_free(q->write_event);
    q->read_event = q->write_event = NULL;

    if(q->rt_mainloop_tmq) {
        q->rt_mainloop_tmq->mainloop->io_free(q->rt_mainloop_tmq->read_event);
        q->rt_mainloop_tmq->mainloop->io_free(q->rt_mainloop_tmq->write_event);
        q->rt_mainloop_tmq->read_event = q->rt_mainloop_tmq->write_event = NULL;
    }

    pa_asyncmsgq_unref(q->inq);
    pa_asyncmsgq_unref(q->outq);
    q->inq = q->outq = NULL;

    q->mainloop = NULL;
}

void pa_thread_mq_install(pa_thread_mq *q) {
    pa_assert(q);

    pa_assert(!(PA_STATIC_TLS_GET(thread_mq)));
    PA_STATIC_TLS_SET(thread_mq, q);
}

pa_thread_mq *pa_thread_mq_get(void) {
    return PA_STATIC_TLS_GET(thread_mq);
}
