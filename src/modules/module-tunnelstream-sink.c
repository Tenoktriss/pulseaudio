/***
    This file is part of PulseAudio.

    Copyright 2013 Alexander Couzens

    PulseAudio is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation; either version 2.1 of the License,
    or (at your option) any later version.

    PulseAudio is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with PulseAudio; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
    USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulse/rtclock.h>
#include <pulse/timeval.h>
#include <pulse/xmalloc.h>

#include <pulsecore/core.h>
#include <pulsecore/core-util.h>
#include <pulsecore/i18n.h>
#include <pulsecore/sink.h>
#include <pulsecore/modargs.h>
#include <pulsecore/log.h>
#include <pulsecore/thread.h>
#include <pulsecore/thread-mq.h>
#include <pulsecore/rtpoll.h>

#include "module-tunnelstream-sink-symdef.h"

PA_MODULE_AUTHOR("Alexander Couzens");
PA_MODULE_DESCRIPTION(_("Create a network sink which connects via a stream to a remote pulseserver"));
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(FALSE);
PA_MODULE_USAGE(
        _("sink_name=<name of sink>"));

#define DEFAULT_SINK_NAME "auto_null"

#define MEMBLOCKQ_MAXLENGTH (16*1024*1024)


struct userdata {
    pa_module *module;

    pa_sink *sink;
    pa_sink_input *sink_input;
    pa_rtpoll *rtpoll;
    pa_thread_mq thread_mq;
    pa_thread *thread;

    pa_memblockq *memblockq;

    pa_bool_t auto_desc;

    unsigned channels;
    pa_usec_t block_usec;
    pa_usec_t timestamp;
};

static const char* const valid_modargs[] = {
    "sink_name",
    "sink_properties",
    "remote_server",
    "remote_port",
    NULL,
};

static void thread_func(void *userdata) {
    struct userdata *u = userdata;

    pa_assert(u);

    pa_log_debug("Tunnelstream: Thread starting up");

    pa_thread_mq_install(&u->thread_mq);

    u->timestamp = pa_rtclock_now();

    for(;;)
    {
        pa_usec_t now = 0;
        int ret;

        if (PA_SINK_IS_OPENED(u->sink->thread_info.state))
            now = pa_rtclock_now();

//        if (PA_UNLIKELY(u->sink->thread_info.rewind_requested))
//            process_rewind(u, now);


        /* Hmm, nothing to do. Let's sleep */
        if ((ret = pa_rtpoll_run(u->rtpoll, TRUE)) < 0)
            goto fail;

        if (ret == 0)
            goto finish;
    }
fail:
    /* If this was no regular exit from the loop we have to continue
     * processing messages until we received PA_MESSAGE_SHUTDOWN */
    pa_asyncmsgq_post(u->thread_mq.outq, PA_MSGOBJECT(u->module->core), PA_CORE_MESSAGE_UNLOAD_MODULE, u->module, 0, NULL, NULL);
    pa_asyncmsgq_wait_for(u->thread_mq.inq, PA_MESSAGE_SHUTDOWN);

finish:
    pa_log_debug("Thread shutting down");
}

int pa__init(pa_module*m) {
    struct userdata *u;
    pa_modargs *ma;
    pa_sink_new_data sink_data;
    pa_sample_spec ss;
    pa_channel_map map;
//    pa_sink_input_new_data sink_input_data;

    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("Failed to parse module arguments.");
        goto fail;
    }

    ss = m->core->default_sample_spec;
    map = m->core->default_channel_map;
    if (pa_modargs_get_sample_spec_and_channel_map(ma, &ss, &map, PA_CHANNEL_MAP_DEFAULT) < 0) {
        pa_log("Invalid sample format specification or channel map");
        goto fail;
    }

    u = pa_xnew0(struct userdata, 1);
    u->module = m;
    m->userdata = u;
    u->rtpoll = pa_rtpoll_new();
    pa_thread_mq_init(&u->thread_mq, m->core->mainloop, u->rtpoll);

    /* Create sink */
    pa_sink_new_data_init(&sink_data);
    sink_data.driver = __FILE__;
    sink_data.module = m;

    pa_sink_new_data_set_name(&sink_data, pa_modargs_get_value(ma, "sink_name", DEFAULT_SINK_NAME));
    pa_sink_new_data_set_sample_spec(&sink_data, &ss);
    pa_sink_new_data_set_channel_map(&sink_data, &map);
    // TODO: set DEVICE CLASS
    pa_proplist_sets(sink_data.proplist, PA_PROP_DEVICE_CLASS, "abstract");
    pa_proplist_sets(sink_data.proplist, PA_PROP_DEVICE_DESCRIPTION, _("Null Output"));

    if (pa_modargs_get_proplist(ma, "sink_properties", sink_data.proplist, PA_UPDATE_REPLACE) < 0) {
        pa_log("Invalid properties");
        pa_sink_new_data_done(&sink_data);
        goto fail;
    }

    u->sink = pa_sink_new(m->core, &sink_data, (PA_SINK_LATENCY|PA_SINK_DYNAMIC_LATENCY));
    pa_sink_new_data_done(&sink_data);

    if (!u->sink) {
        pa_log("Failed to create sink.");
        goto fail;
    }

    u->sink->userdata = u;
    /* callbacks */
//    u->sink->parent.process_msg = sink_process_msg;
//    u->sink->update_requested_latency = sink_update_requested_latency_cb;
    pa_sink_set_asyncmsgq(u->sink, u->thread_mq.inq);
    pa_sink_set_rtpoll(u->sink, u->rtpoll);


//    u->block_usec = BLOCK_USEC;
//    nbytes = pa_usec_to_bytes(u->block_usec, &u->sink->sample_spec);
//    pa_sink_set_max_rewind(u->sink, nbytes);
//    pa_sink_set_max_request(u->sink, nbytes);

    if (!(u->thread = pa_thread_new("tunnelstream-sink", thread_func, u))) {
        pa_log("Failed to create thread.");
        goto fail;
    }

//    pa_sink_set_latency_range(u->sink, 0, BLOCK_USEC);
// bis hierhin kommt er
    pa_sink_put(u->sink);

//    u->memblockq = pa_memblockq_new("module-virtual-sink memblockq", 0, MEMBLOCKQ_MAXLENGTH, 0, &ss, 1, 1, 0, NULL);
    pa_modargs_free(ma);

    // TODO: think about volume stuff remote<--stream--source

    return 0;

fail:
    if (ma)
        pa_modargs_free(ma);

    pa__done(m);

    return -1;
}

void pa__done(pa_module*m) {
    struct userdata *u;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    /* See comments in sink_input_kill_cb() above regarding
     * destruction order! */

    if (u->sink_input)
        pa_sink_input_unlink(u->sink_input);

    if (u->sink)
        pa_sink_unlink(u->sink);

    if (u->sink_input)
        pa_sink_input_unref(u->sink_input);

    if (u->sink)
        pa_sink_unref(u->sink);

    if (u->memblockq)
        pa_memblockq_free(u->memblockq);

    pa_xfree(u);
}
