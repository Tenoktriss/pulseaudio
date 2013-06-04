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

#include "module-tunnelstream-symdef.h"

PA_MODULE_AUTHOR("Alexander Couzens");
PA_MODULE_DESCRIPTION(_("Create a network sink which connects via a stream to a remote pulseserver"));
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(TRUE);
PA_MODULE_USAGE(
        "sink_name=<name of sink>");

#define DEFAULT_SINK_NAME "auto_null"

static const char* const valid_modargs[] = {
    "sink_name",
    NULL,
};

int pa__init(pa_module*m) {
    return 0;
}

void pa__done(pa_module*m) {
}
