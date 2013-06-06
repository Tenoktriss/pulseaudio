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

#include <pulse/xmalloc.h>

#include <pulsecore/core.h>
#include <pulsecore/core-util.h>
#include <pulsecore/i18n.h>
#include <pulsecore/sink.h>
#include <pulsecore/modargs.h>
#include <pulsecore/log.h>

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

    /* FIXME: Uncomment this and take "autoloaded" as a modarg if this is a filter */
    /* pa_bool_t autoloaded; */

    pa_sink *sink;
    pa_sink_input *sink_input;

    pa_memblockq *memblockq;

    pa_bool_t auto_desc;
    unsigned channels;
};

static const char* const valid_modargs[] = {
    "sink_name",
    "sink_properties",
    "remote_server",
    "remote_port",
    NULL,
};

int pa__init(pa_module*m) {
    /* most stuff is copy&pasted from module-virtual-sink and adapted to this case ... */
    struct userdata *u;
    pa_modargs *ma;
    pa_sink_new_data sink_data;
    pa_sample_spec ss;
    pa_channel_map map;
//    pa_memchunk silence;
    pa_sink_input_new_data sink_input_data;

    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("Failed to parse module arguments.");
        goto fail;
    }

    u = pa_xnew0(struct userdata, 1);
    u->module = m;
    m->userdata = u;

    ss.channels = 2;
    ss.format = PA_SAMPLE_FLOAT32;
    // TODO: look at the sample spec + channel map stuff - do we really need that?

    /* Create sink */
    pa_sink_new_data_init(&sink_data);
    sink_data.driver = __FILE__;
    sink_data.module = m;

    // TODO use remoteserver/port name for sink
    if (!(sink_data.name = pa_xstrdup(pa_modargs_get_value(ma, "sink_name", NULL))))
        sink_data.name = pa_sprintf_malloc("rsink");
    pa_sink_new_data_set_sample_spec(&sink_data, &ss);
    pa_sink_new_data_set_channel_map(&sink_data, &map);
    // TODO: what tell mod-tunnel to this prop?
//    pa_proplist_sets(sink_data.proplist, PA_PROP_DEVICE_CLASS, "filter");
    pa_proplist_sets(sink_data.proplist, "device.vsink.name", sink_data.name);

    if (pa_modargs_get_proplist(ma, "sink_properties", sink_data.proplist, PA_UPDATE_REPLACE) < 0) {
        pa_log("Invalid properties");
        pa_sink_new_data_done(&sink_data);
        goto fail;
    }

    if ((u->auto_desc = !pa_proplist_contains(sink_data.proplist, PA_PROP_DEVICE_DESCRIPTION))) {
        const char *z;
        // TODO: adapt this and fill it with correct data
    }

    // TODO check this call!
    u->sink = pa_sink_new(m->core, &sink_data, (PA_SINK_LATENCY|PA_SINK_DYNAMIC_LATENCY));
    pa_sink_new_data_done(&sink_data);

    if (!u->sink) {
        pa_log("Failed to create sink.");
        goto fail;
    }

    // TODO: set callbacks!
    // TODO: think about volume stuff remote<--stream--source
    u->sink->userdata = u;


    /* Create sink input */
    pa_sink_input_new_data_init(&sink_input_data);
    sink_input_data.driver = __FILE__;
    sink_input_data.module = m;
//    pa_sink_input_new_data_set_sink(&sink_input_data, master, FALSE);
    sink_input_data.origin_sink = u->sink;
    pa_proplist_setf(sink_input_data.proplist, PA_PROP_MEDIA_NAME, "Virtual Sink Stream from %s", pa_proplist_gets(u->sink->proplist, PA_PROP_DEVICE_DESCRIPTION));
    pa_proplist_sets(sink_input_data.proplist, PA_PROP_MEDIA_ROLE, "filter");
    pa_sink_input_new_data_set_sample_spec(&sink_input_data, &ss);
    pa_sink_input_new_data_set_channel_map(&sink_input_data, &map);

    pa_sink_input_new(&u->sink_input, m->core, &sink_input_data);
    pa_sink_input_new_data_done(&sink_input_data);

    if (!u->sink_input)
        goto fail;
    // TODO: callbacks sink input <- do we need sink_input or simethink else?
    u->sink_input->userdata = u;


    // TODO: handle *silence in the correct way!
    //pa_sink_input_get_silence(u->sink_input, &silence);
    u->memblockq = pa_memblockq_new("module-virtual-sink memblockq", 0, MEMBLOCKQ_MAXLENGTH, 0, &ss, 1, 1, 0, NULL);
//    pa_memblock_unref(silence.memblock);

//    pa_sink_put(u->sink);
//    pa_sink_input_put(u->sink_input);

    pa_modargs_free(ma);

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
