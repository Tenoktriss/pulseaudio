/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB
  Copyright 2013 Alexander Couzens <lynxis@fe80.eu>

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

void pa_rtmainloop_read_work(pa_mainloop_api *ea, pa_io_event *e, int fd, pa_io_event_flags_t events, void *userdata) {
    pa_msgobject *object;
    int code;
    void *data;
    pa_memchunk chunk;
    int64_t offset;

    pa_assert(i);

    if (pa_asyncmsgq_get(userdata, &object, &code, &data, &offset, &chunk, 0) == 0) {
        int ret;

        if (!object && code == PA_MESSAGE_SHUTDOWN) {
            pa_asyncmsgq_done(i->userdata, 0);
            pa_rtpoll_quit(i->rtpoll);
        }

        ret = pa_asyncmsgq_dispatch(object, code, data, offset, &chunk);
        pa_asyncmsgq_done(i->userdata, ret);
    }
}

void pa_rtmainloop_write_work(pa_mainloop_api *ea, pa_io_event *e, int fd, pa_io_event_flags_t events, void *userdata) {

}
