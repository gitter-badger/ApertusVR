/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/* This file contains both the uv__async internal infrastructure and the
 * user-facing uv_async_t functions.
 */

#include "uv.h"
#include "internal.h"
#include "atomic-ops.h"

#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void uv__async_event(uv_loop_t* loop,
                            struct uv__async* w,
                            unsigned int nevents);
static int uv__async_eventfd(void);


int uv_async_init(uv_loop_t* loop, uv_async_t* handle, uv_async_cb async_cb) {
  if (uv__async_start(loop, &loop->async_watcher, uv__async_event))
    return uv__set_sys_error(loop, errno);

  uv__handle_init(loop, (uv_handle_t*)handle, UV_ASYNC);
  handle->async_cb = async_cb;
  handle->pending = 0;

  ngx_queue_insert_tail(&loop->async_handles, &handle->queue);
  uv__handle_start(handle);

  return 0;
}


int uv_async_send(uv_async_t* handle) {
  /* Do a cheap read first. */
  if (ACCESS_ONCE(int, handle->pending) != 0)
    return 0;

  if (cmpxchgi(&handle->pending, 0, 1) == 0)
    uv__async_send(&handle->loop->async_watcher);

  return 0;
}


void uv__async_close(uv_async_t* handle) {
  ngx_queue_remove(&handle->queue);
  uv__handle_stop(handle);
}


static void uv__async_event(uv_loop_t* loop,
                            struct uv__async* w,
                            unsigned int nevents) {
  ngx_queue_t queue;
  ngx_queue_t* q;
  uv_async_t* h;

  ngx_queue_move(&loop->async_handles, &queue);
  while (!ngx_queue_empty(&queue)) {
    q = ngx_queue_head(&queue);
    h = ngx_queue_data(q, uv_async_t, queue);

    ngx_queue_remove(q);
    ngx_queue_insert_tail(&loop->async_handles, q);

    if (cmpxchgi(&h->pending, 1, 0) == 0)
      continue;

    h->async_cb(h, 0);
  }
}


static void uv__async_io(uv_loop_t* loop, uv__io_t* w, unsigned int events) {
  struct uv__async* wa;
  char buf[1024];
  unsigned n;
  ssize_t r;

  n = 0;
  for (;;) {
    r = read(w->fd, buf, sizeof(buf));

    if (r > 0)
      n += r;

    if (r == sizeof(buf))
      continue;

    if (r != -1)
      break;

    if (errno == EAGAIN || errno == EWOULDBLOCK)
      break;

    if (errno == EINTR)
      continue;

    abort();
  }

  wa = container_of(w, struct uv__async, io_watcher);

#if defined(__linux__)
  if (wa->wfd == -1) {
    uint64_t val;
    assert(n == sizeof(val));
    memcpy(&val, buf, sizeof(val));  /* Avoid alignment issues. */
    wa->cb(loop, wa, val);
    return;
  }
#endif

  wa->cb(loop, wa, n);
}


void uv__async_send(struct uv__async* wa) {
  const void* buf;
  ssize_t len;
  int fd;
  int r;

  buf = "";
  len = 1;
  fd = wa->wfd;

#if defined(__linux__)
  if (fd == -1) {
    static const uint64_t val = 1;
    buf = &val;
    len = sizeof(val);
    fd = wa->io_watcher.fd;  /* eventfd */
  }
#endif

  do
    r = write(fd, buf, len);
  while (r == -1 && errno == EINTR);

  if (r == len)
    return;

  if (r == -1)
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return;

  abort();
}


void uv__async_init(struct uv__async* wa) {
  wa->io_watcher.fd = -1;
  wa->wfd = -1;
}


int uv__async_start(uv_loop_t* loop, struct uv__async* wa, uv__async_cb cb) {
  int pipefd[2];
  int fd;

  if (wa->io_watcher.fd != -1)
    return 0;

  fd = uv__async_eventfd();
  if (fd >= 0) {
    pipefd[0] = fd;
    pipefd[1] = -1;
  }
  else if (fd != -ENOSYS)
    return -1;
  else if (uv__make_pipe(pipefd, UV__F_NONBLOCK))
    return -1;

  uv__io_init(&wa->io_watcher, uv__async_io, pipefd[0]);
  uv__io_start(loop, &wa->io_watcher, UV__POLLIN);
  wa->wfd = pipefd[1];
  wa->cb = cb;

  return 0;
}


void uv__async_stop(uv_loop_t* loop, struct uv__async* wa) {
  if (wa->io_watcher.fd == -1)
    return;

  uv__io_stop(loop, &wa->io_watcher, UV__POLLIN);
  close(wa->io_watcher.fd);
  wa->io_watcher.fd = -1;

  if (wa->wfd != -1) {
    close(wa->wfd);
    wa->wfd = -1;
  }
}


static int uv__async_eventfd() {
#if defined(__linux__)
  static int no_eventfd2;
  static int no_eventfd;
  int fd;

  if (no_eventfd2)
    goto skip_eventfd2;

  fd = uv__eventfd2(0, UV__EFD_CLOEXEC | UV__EFD_NONBLOCK);
  if (fd != -1)
    return fd;

  if (errno != ENOSYS)
    return -errno;

  no_eventfd2 = 1;

skip_eventfd2:

  if (no_eventfd)
    goto skip_eventfd;

  fd = uv__eventfd(0);
  if (fd != -1) {
    uv__cloexec(fd, 1);
    uv__nonblock(fd, 1);
    return fd;
  }

  if (errno != ENOSYS)
    return -errno;

  no_eventfd = 1;

skip_eventfd:

#endif

  return -ENOSYS;
}