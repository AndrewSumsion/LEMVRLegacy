/*
 * QEMU aio implementation
 *
 * Copyright IBM Corp., 2008
 * Copyright Red Hat Inc., 2012
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Paolo Bonzini     <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "block/block.h"
#include "qemu/main-loop.h"
#include "qemu/queue.h"
#include "qemu/sockets.h"
#include "qapi/error.h"
#include "qemu/rcu_queue.h"

struct AioHandler {
    EventNotifier *e;
    IOHandler *io_read;
    IOHandler *io_write;
    EventNotifierHandler *io_notify;
    GPollFD pfd;
    int deleted;
    void *opaque;
    bool is_external;
    QLIST_ENTRY(AioHandler) node;
};

void aio_set_fd_handler(AioContext *ctx,
                        int fd,
                        bool is_external,
                        IOHandler *io_read,
                        IOHandler *io_write,
                        AioPollFn *io_poll,
                        void *opaque)
{
    /* fd is a SOCKET in our case */
    AioHandler *node;

    qemu_lockcnt_lock(&ctx->list_lock);
    QLIST_FOREACH(node, &ctx->aio_handlers, node) {
        if (node->pfd.fd == fd && !node->deleted) {
            break;
        }
    }

    /* Are we deleting the fd handler? */
    if (!io_read && !io_write) {
        assert(!node->io_notify);
        /* Detach the event */
        WSAEventSelect(node->pfd.fd, NULL, 0);

        if (node) {
            /* If aio_poll is in progress, just mark the node as deleted */
            if (qemu_lockcnt_count(&ctx->list_lock)) {
                node->deleted = 1;
                node->pfd.revents = 0;
            } else {
                /* Otherwise, delete it for real.  We can't just mark it as
                 * deleted because deleted nodes are only cleaned up after
                 * releasing the list_lock.
                 */
                QLIST_REMOVE(node, node);
                g_free(node);
            }
        }
    } else {
        HANDLE event;
        long bitmask = FD_OOB;

        if (node == NULL) {
            /* Alloc and insert if it's not already there */
            node = g_new0(AioHandler, 1);
            node->pfd.fd = fd;
            QLIST_INSERT_HEAD_RCU(&ctx->aio_handlers, node, node);
        }

        node->pfd.events = 0;
        if (node->io_read) {
            node->pfd.events |= G_IO_IN;
        }
        if (node->io_write) {
            node->pfd.events |= G_IO_OUT;
        }

        node->e = &ctx->notifier;

        /* Update handler with latest information */
        node->opaque = opaque;
        node->io_read = io_read;
        node->io_write = io_write;
        node->is_external = is_external;

        if (io_read) {
            bitmask |= FD_READ | FD_ACCEPT | FD_CLOSE | FD_CONNECT;
        }

        if (io_write) {
            bitmask |= FD_WRITE | FD_CONNECT;
        }

        event = event_notifier_get_handle(&ctx->notifier);
        WSAEventSelect(node->pfd.fd, event, bitmask);
    }

    qemu_lockcnt_unlock(&ctx->list_lock);
    aio_notify(ctx);
}

void aio_set_fd_poll(AioContext *ctx, int fd,
                     IOHandler *io_poll_begin,
                     IOHandler *io_poll_end)
{
    /* Not implemented */
}

void aio_set_event_notifier(AioContext *ctx,
                            EventNotifier *e,
                            bool is_external,
                            EventNotifierHandler *io_notify,
                            AioPollFn *io_poll)
{
    AioHandler *node;

    qemu_lockcnt_lock(&ctx->list_lock);
    QLIST_FOREACH(node, &ctx->aio_handlers, node) {
        if (node->e == e && !node->deleted) {
            break;
        }
    }

    /* Are we deleting the fd handler? */
    if (!io_notify) {
        if (node) {
            g_source_remove_poll(&ctx->source, &node->pfd);

            /* aio_poll is in progress, just mark the node as deleted */
            if (qemu_lockcnt_count(&ctx->list_lock)) {
                node->deleted = 1;
                node->pfd.revents = 0;
            } else {
                /* Otherwise, delete it for real.  We can't just mark it as
                 * deleted because deleted nodes are only cleaned up after
                 * releasing the list_lock.
                 */
                QLIST_REMOVE(node, node);
                g_free(node);
            }
        }
    } else {
        if (node == NULL) {
            /* Alloc and insert if it's not already there */
            node = g_new0(AioHandler, 1);
            node->e = e;
            node->pfd.fd = (uintptr_t)event_notifier_get_handle(e);
            node->pfd.events = G_IO_IN;
            node->is_external = is_external;
            QLIST_INSERT_HEAD_RCU(&ctx->aio_handlers, node, node);

            g_source_add_poll(&ctx->source, &node->pfd);
        }
        /* Update handler with latest information */
        node->io_notify = io_notify;
    }

    qemu_lockcnt_unlock(&ctx->list_lock);
    aio_notify(ctx);
}

void aio_set_event_notifier_poll(AioContext *ctx,
                                 EventNotifier *notifier,
                                 EventNotifierHandler *io_poll_begin,
                                 EventNotifierHandler *io_poll_end)
{
    /* Not implemented */
}

bool aio_prepare(AioContext *ctx)
{
    AioHandler *node;
    WSAPOLLFD fds[128];
    bool have_select_revents = false;

    int i = 0;
    int polled_count = 0;

    /*
     * We have to walk very carefully in case aio_set_fd_handler is
     * called while we're walking.
     */
    qemu_lockcnt_inc(&ctx->list_lock);

    QLIST_FOREACH(node, &ctx->aio_handlers, node) {
        if (i >= sizeof(fds) / sizeof(*fds)) {
            break;
        }

        if (node->deleted || (!node->io_read && !node->io_write)) {
            fds[i].fd = -1; // ignore
        } else {
            fds[i].fd = node->pfd.fd;
            fds[i].events = (node->io_read ? POLLIN : 0) | (node->io_write ? POLLOUT : 0);
            ++polled_count;
        }
        ++i;
    }

    if (polled_count == 0) {
        return false;
    }

    // aio_prepare() is called very often on Windows, and every call takes
    // at least 5 us, with most coming closer to 20 us.
    // Let's make sure we don't prevent all other vCPUs from running during
    // this time.
    const bool had_iothread_lock = qemu_mutex_iothread_locked();
    if (had_iothread_lock) {
        qemu_mutex_unlock_iothread();
    }

    const int fds_count = i;
    const int poll_res = WSAPoll(fds, fds_count, 0);

    if (had_iothread_lock) {
        qemu_mutex_lock_iothread();
    }

    if (poll_res > 0) {
        i = 0;
        QLIST_FOREACH(node, &ctx->aio_handlers, node) {
            node->pfd.revents = 0;
            if (i < fds_count && fds[i].fd >= 0) {
                if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                    node->pfd.revents |= G_IO_IN;
                    have_select_revents = true;
                }
                if (fds[i].revents & POLLOUT) {
                    node->pfd.revents |= G_IO_OUT;
                    have_select_revents = true;
                }
            }
            ++i;
        }
    }

    qemu_lockcnt_dec(&ctx->list_lock);
    return have_select_revents;
}

bool aio_pending(AioContext *ctx)
{
    AioHandler *node;
    bool result = false;

    /*
     * We have to walk very carefully in case aio_set_fd_handler is
     * called while we're walking.
     */
    qemu_lockcnt_inc(&ctx->list_lock);
    QLIST_FOREACH_RCU(node, &ctx->aio_handlers, node) {
        if (node->pfd.revents && node->io_notify) {
            result = true;
            break;
        }

        if ((node->pfd.revents & G_IO_IN) && node->io_read) {
            result = true;
            break;
        }
        if ((node->pfd.revents & G_IO_OUT) && node->io_write) {
            result = true;
            break;
        }
    }

    qemu_lockcnt_dec(&ctx->list_lock);
    return result;
}

static bool aio_dispatch_handlers(AioContext *ctx, HANDLE event)
{
    AioHandler *node;
    bool progress = false;
    AioHandler *tmp;

    /*
     * We have to walk very carefully in case aio_set_fd_handler is
     * called while we're walking.
     */
    QLIST_FOREACH_SAFE_RCU(node, &ctx->aio_handlers, node, tmp) {
        int revents = node->pfd.revents;

        if (!node->deleted &&
            (revents || event_notifier_get_handle(node->e) == event) &&
            node->io_notify) {
            node->pfd.revents = 0;
            node->io_notify(node->e);

            /* aio_notify() does not count as progress */
            if (node->e != &ctx->notifier) {
                progress = true;
            }
        }

        if (!node->deleted &&
            (node->io_read || node->io_write)) {
            node->pfd.revents = 0;
            if ((revents & G_IO_IN) && node->io_read) {
                node->io_read(node->opaque);
                progress = true;
            }
            if ((revents & G_IO_OUT) && node->io_write) {
                node->io_write(node->opaque);
                progress = true;
            }

            /* if the next select() will return an event, we have progressed */
            if (event == event_notifier_get_handle(&ctx->notifier) ||
                (event == INVALID_HANDLE_VALUE && node->e == &ctx->notifier)) {
                WSANETWORKEVENTS ev;
                WSAEnumNetworkEvents(node->pfd.fd, event_notifier_get_handle(&ctx->notifier), &ev);
                if (ev.lNetworkEvents) {
                    progress = true;
                }
            }
        }

        if (node->deleted) {
            if (qemu_lockcnt_dec_if_lock(&ctx->list_lock)) {
                QLIST_REMOVE(node, node);
                g_free(node);
                qemu_lockcnt_inc_and_unlock(&ctx->list_lock);
            }
        }
    }

    return progress;
}

void aio_dispatch(AioContext *ctx)
{
    qemu_lockcnt_inc(&ctx->list_lock);
    aio_bh_poll(ctx);
    aio_dispatch_handlers(ctx, INVALID_HANDLE_VALUE);
    qemu_lockcnt_dec(&ctx->list_lock);
    timerlistgroup_run_timers(&ctx->tlg);
}

bool aio_poll(AioContext *ctx, bool blocking)
{
    AioHandler *node;
    HANDLE events[MAXIMUM_WAIT_OBJECTS + 1];
    bool progress, have_select_revents, first;
    int count;
    int timeout;

    progress = false;

    /* aio_notify can avoid the expensive event_notifier_set if
     * everything (file descriptors, bottom halves, timers) will
     * be re-evaluated before the next blocking poll().  This is
     * already true when aio_poll is called with blocking == false;
     * if blocking == true, it is only true after poll() returns,
     * so disable the optimization now.
     */
    if (blocking) {
        atomic_add(&ctx->notify_me, 2);
    }

    qemu_lockcnt_inc(&ctx->list_lock);
    have_select_revents = aio_prepare(ctx);

    /* fill fd sets */
    count = 0;
    QLIST_FOREACH_RCU(node, &ctx->aio_handlers, node) {
        if (!node->deleted && node->io_notify
            && aio_node_check(ctx, node->is_external)) {
            events[count++] = event_notifier_get_handle(node->e);
        }
    }

    first = true;

    /* ctx->notifier is always registered.  */
    assert(count > 0);

    /* Multiple iterations, all of them non-blocking except the first,
     * may be necessary to process all pending events.  After the first
     * WaitForMultipleObjects call ctx->notify_me will be decremented.
     */
    do {
        HANDLE event;
        int ret;

        timeout = blocking && !have_select_revents
            ? qemu_timeout_ns_to_ms(aio_compute_timeout(ctx)) : 0;
        ret = WaitForMultipleObjects(count, events, FALSE, timeout);
        if (blocking) {
            assert(first);
            atomic_sub(&ctx->notify_me, 2);
        }

        if (first) {
            aio_notify_accept(ctx);
            progress |= aio_bh_poll(ctx);
            first = false;
        }

        /* if we have any signaled events, dispatch event */
        event = NULL;
        if ((DWORD) (ret - WAIT_OBJECT_0) < count) {
            event = events[ret - WAIT_OBJECT_0];
            events[ret - WAIT_OBJECT_0] = events[--count];
        } else if (!have_select_revents) {
            break;
        }

        have_select_revents = false;
        blocking = false;

        progress |= aio_dispatch_handlers(ctx, event);
    } while (count > 0);

    qemu_lockcnt_dec(&ctx->list_lock);

    progress |= timerlistgroup_run_timers(&ctx->tlg);
    return progress;
}

void aio_context_setup(AioContext *ctx)
{
}

void aio_context_set_poll_params(AioContext *ctx, int64_t max_ns,
                                 int64_t grow, int64_t shrink, Error **errp)
{
    if (max_ns) {
        error_setg(errp, "AioContext polling is not implemented on Windows");
    }
}
