// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 RFNM

/*
 * tcp_module.c - Fixed version
 * RFNM Daughterboard Driver with UDP discovery and TCP control/data channels
 * Fixed to handle multiple simultaneous client connections without crashing
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/jiffies.h>
#include <linux/inet.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <net/sock.h>
#include <linux/uio.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/completion.h>
#include <linux/wait.h>
#include <linux/sched.h>

#include <linux/rfnm-shared.h>
#include <linux/rfnm-api.h>

#define BUFFER_SIZE_TCP_CTRL     1152

/* Module parameters */
static char local_ip[16] = "0.0.0.0";
module_param_string(local_ip, local_ip, sizeof(local_ip), 0644);
MODULE_PARM_DESC(local_ip, "Local IP address to bind to");

/* UDP discovery socket for broadcast responses */
struct udp_discovery {
    struct socket      *sock;
    struct sockaddr_in  bind_addr;
    struct task_struct *thread;
    bool                running;
};

static struct udp_discovery *udp_disc = NULL;

/* TCP service container */
struct tcp_service {
    struct socket      *listen_sock;
    struct socket      *conn_sock;
    struct sockaddr_in  bind_addr;
    struct task_struct *accept_thread;
    struct task_struct *worker_thread;
    bool                running;
    bool                worker_running;
    struct mutex        conn_lock;      /* Protects conn_sock */
    struct mutex        send_lock;      /* Protects sends */
    struct completion   worker_done;    /* Signals worker termination */
    wait_queue_head_t   conn_wait;     /* Wait for new connections */
    int                 port;
    const char         *name;
    struct socket      *dead_sock;      /* Retired connection awaiting release by the worker(s) */
    atomic_t            dead_users;     /* workers still holding the retired socket; last one releases */
    struct task_struct *worker2_thread; /* data service only: second (per-direction) worker */
    bool                worker2_running;
    struct completion   worker2_done;
    void              (*listen_orig_data_ready)(struct sock *sk);
};

static struct tcp_service tcp_ctrl, tcp_data;

/* accept must not cost latency: a fresh client's first packets sit in the
 * unaccepted socket's queue, so a polled accept (the old msleep(100) + a 50 ms
 * settle) put 50-150 ms dead time in front of EVERY new session - measured as
 * the schedule floor for remote timed TX. The listen socket's data-ready hook
 * (fires on a completed handshake) wakes the accept thread instead; the accept
 * queue length is the wait condition so a wake racing the EAGAIN is never lost. */
static DECLARE_WAIT_QUEUE_HEAD(tcp_accept_wq);
static void tcp_listen_ready_wake(struct sock *sk)
{
    struct tcp_service *svc = sk->sk_user_data;

    if (svc && svc->listen_orig_data_ready)
        svc->listen_orig_data_ready(sk);
    wake_up(&tcp_accept_wq);
}

static bool tcp_accept_pending(struct tcp_service *svc)
{
    return READ_ONCE(svc->listen_sock->sk->sk_ack_backlog) != 0;
}

/* RFNM helper declarations */
int rfnm_queue_local_buffer_tx(uint8_t *buf, uint32_t len);
void *rfnm_claim_local_buffer_tx(uint8_t **buf);
void rfnm_commit_local_buffer_tx(void *handle);
void rfnm_abort_local_buffer_tx(void *handle);
int rfnm_dequeue_local_buffer_rx(uint8_t *buf);
void *rfnm_dequeue_local_buffer_rx_ref(uint8_t **buf);
void rfnm_release_local_buffer_rx_ref(void *handle);
int rfnm_local_buffer_rx_available(void);
int rfnm_local_buffer_tx_free(void);
void rfnm_tx_flush_staging(int scrub_ring);
extern wait_queue_head_t local_rx_poll;
extern wait_queue_head_t local_tx_poll;
int rfnm_restart_sm(int val);

/* ingest-only wake channel: the socket data-ready hook used to wake local_rx_poll,
 * so every incoming TCP segment also rescheduled the board->host TX worker and every
 * produced RX buffer woke the ingest worker - two busy threads cross-waking each
 * other at tens of kHz for no reason. Each waitqueue now has one waiter class. */
static DECLARE_WAIT_QUEUE_HEAD(eth_ingest_wq);

/* packets coalesced into one sendmsg on the data path; 16 measured equal, so
 * keep the batch small to hold fewer pool buffers across the send */
#define RFNM_ETH_TX_BATCH 4

/* ------------------------------------------------------------------ */
/* UDP Discovery Functions */

static int udp_discovery_thread(void *data)
{
    struct udp_discovery *disc = data;
    struct msghdr msg;
    struct kvec kvec;
    char buffer[256];
    struct sockaddr_in client_addr;
    int ret;

    pr_info("UDP discovery thread started\n");

    while (disc->running) {
        memset(&msg, 0, sizeof(msg));
        memset(&client_addr, 0, sizeof(client_addr));

        kvec.iov_base = buffer;
        kvec.iov_len = sizeof(buffer);

        msg.msg_name = &client_addr;
        msg.msg_namelen = sizeof(client_addr);

        ret = kernel_recvmsg(disc->sock, &msg, &kvec, 1,
                             sizeof(buffer), MSG_DONTWAIT);
        
        if (ret > 0) {
            uint8_t cmd = buffer[0];
            uint32_t size;
            
            /* Only respond to discovery/hwinfo requests */
            if (cmd == (RFNM_GET_DEV_HWINFO & 0xff)) {
                if (rfnm_dev_process_udp_ctrl(cmd, &size, (uint8_t *)&buffer[1])) {
                    /* Send response back to broadcaster */
                    msg.msg_name = &client_addr;
                    msg.msg_namelen = sizeof(client_addr);
                    kvec.iov_base = buffer;
                    kvec.iov_len = size + 1;
                    
                    kernel_sendmsg(disc->sock, &msg, &kvec, 1, size + 1);
                    
                    pr_info("Discovery response sent to %pI4:%d\n",
                            &client_addr.sin_addr,
                            ntohs(client_addr.sin_port));
                }
            }
        } else if (ret == -EAGAIN || ret == -EWOULDBLOCK) {
            schedule_timeout_idle(msecs_to_jiffies(100));    /* TASK_IDLE: keep this poll out of loadavg */
        } else if (ret < 0) {
            pr_err("UDP discovery receive error: %d\n", ret);
            schedule_timeout_idle(msecs_to_jiffies(100));
        }
    }

    pr_info("UDP discovery thread terminated\n");
    return 0;
}

static int init_udp_discovery(void)
{
    int ret;
    struct udp_discovery *disc;

    disc = kzalloc(sizeof(*disc), GFP_KERNEL);
    if (!disc)
        return -ENOMEM;

    ret = sock_create_kern(&init_net, AF_INET, SOCK_DGRAM, 
                           IPPROTO_UDP, &disc->sock);
    if (ret < 0) {
        pr_err("Failed to create UDP discovery socket: %d\n", ret);
        kfree(disc);
        return ret;
    }

    /* Bind to discovery port */
    memset(&disc->bind_addr, 0, sizeof(disc->bind_addr));
    disc->bind_addr.sin_family = AF_INET;
    disc->bind_addr.sin_port = htons(RFNM_UDP_CTRL_PORT);
    disc->bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    ret = kernel_bind(disc->sock, (struct sockaddr *)&disc->bind_addr,
                      sizeof(disc->bind_addr));
    if (ret < 0) {
        pr_err("Failed to bind UDP discovery port: %d\n", ret);
        sock_release(disc->sock);
        kfree(disc);
        return ret;
    }

    /* Start discovery thread */
    disc->running = true;
    disc->thread = kthread_run(udp_discovery_thread, disc, "rfnm_discovery");
    if (IS_ERR(disc->thread)) {
        ret = PTR_ERR(disc->thread);
        pr_err("Failed to create discovery thread: %d\n", ret);
        sock_release(disc->sock);
        kfree(disc);
        return ret;
    }

    udp_disc = disc;
    pr_info("UDP discovery listening on port %d\n", RFNM_UDP_CTRL_PORT);
    return 0;
}

static void exit_udp_discovery(void)
{
    if (!udp_disc)
        return;

    udp_disc->running = false;
    if (udp_disc->thread)
        kthread_stop(udp_disc->thread);
    
    if (udp_disc->sock) {
        kernel_sock_shutdown(udp_disc->sock, SHUT_RDWR);
        sock_release(udp_disc->sock);
    }

    kfree(udp_disc);
    udp_disc = NULL;
    pr_info("UDP discovery stopped\n");
}

/* ------------------------------------------------------------------ */
/* TCP Functions */

/* returns true if an existing connection was retired (the caller then waits for
 * the worker to drop off the old socket before wiring the new one) */
static bool close_connection_safe(struct tcp_service *svc)
{
    struct socket *old_sock = NULL;
    int waited = 0;

    mutex_lock(&svc->conn_lock);
    if (svc->conn_sock) {
        old_sock = svc->conn_sock;
        svc->conn_sock = NULL;
    }
    mutex_unlock(&svc->conn_lock);

    if (!old_sock)
        return false;

    /* Wake any worker blocked in socket I/O on this connection. The worker is
     * the only I/O user and releases retired sockets from its loop - releasing
     * here can free a socket the worker is still inside recvmsg/sendmsg on
     * (that use-after-free wedged the ctrl service and leaked the worker
     * thread across module unload). */
    kernel_sock_shutdown(old_sock, SHUT_RDWR);

    mutex_lock(&svc->conn_lock);
    while (svc->dead_sock && waited++ < 50) {
        mutex_unlock(&svc->conn_lock);
        msleep(10);
        mutex_lock(&svc->conn_lock);
    }
    if (svc->dead_sock)
        /* worker is wedged; leaking the previous retiree beats freeing a
         * socket it may still be blocked on */
        pr_warn("TCP %s: retired connection not reaped, leaking it\n", svc->name);
    svc->dead_sock = old_sock;
    /* data service: BOTH direction workers hold a reference to the retired
     * connection; the last to notice releases it (ctrl keeps its own protocol) */
    atomic_set(&svc->dead_users, svc->worker2_thread ? 2 : 0);
    mutex_unlock(&svc->conn_lock);
    return true;
}

/* Wake the data worker the moment TCP bytes arrive. Its idle wait used to watch
 * only the RX-direction pool, so a TX-only client was paced purely by the wait
 * timeout (>= 1 jiffy): ~2 packets ingested per jiffy capped TCP TX at a few
 * MSPS regardless of the wire. The original callback is the stock kernel one
 * (identical for every accepted socket), so a single saved pointer is fine. */
static void (*tcp_data_orig_data_ready)(struct sock *sk);
static void tcp_data_ready_wake(struct sock *sk)
{
    if (tcp_data_orig_data_ready)
        tcp_data_orig_data_ready(sk);
    wake_up_interruptible(&eth_ingest_wq);
}

static int tcp_accept_thread(void *arg)
{
    struct tcp_service *svc = arg;
    struct socket *newsock;
    int ret;

    pr_info("TCP accept thread started for %s\n", svc->name);

    while (svc->running) {
        ret = kernel_accept(svc->listen_sock, &newsock, O_NONBLOCK);

        if (ret == -EAGAIN || ret == -EWOULDBLOCK) {
            wait_event_interruptible_timeout(tcp_accept_wq,
                                             !svc->running || tcp_accept_pending(svc),
                                             msecs_to_jiffies(100));
            continue;
        }

        if (ret < 0) {
            if (svc->running)
                pr_err("TCP accept error on %s: %d\n", svc->name, ret);
            break;
        }

        /* Close the existing connection if any; only then does the worker need
         * time to drop off the old socket before we wire the new one */
        if (close_connection_safe(svc))
            msleep(50);

        tcp_sock_set_nodelay(newsock->sk);
        if (svc == &tcp_data) {
            /* sustained sample bursts; don't rely on sndbuf autotuning */
            newsock->sk->sk_userlocks |= SOCK_SNDBUF_LOCK;
            WRITE_ONCE(newsock->sk->sk_sndbuf, 4 * 1024 * 1024);
            tcp_data_orig_data_ready = newsock->sk->sk_data_ready;
            smp_wmb();
            newsock->sk->sk_data_ready = tcp_data_ready_wake;
            /* a dead client's queued TCP bytes keep flowing into the TX pool until
             * its FIN surfaces (the writer may be paused the whole time, so nothing
             * drains them) - seen as ~125 stale packets leading the next session's
             * stream. The old socket is retired above; start the new one clean. */
            rfnm_tx_flush_staging(0);
        }

        /* Set new connection */
        mutex_lock(&svc->conn_lock);
        svc->conn_sock = newsock;
        mutex_unlock(&svc->conn_lock);
        
        pr_info("New TCP connection accepted on %s\n", svc->name);
        
        /* Wake up worker thread */
        wake_up(&svc->conn_wait);
    }

    pr_info("TCP accept thread %s exiting\n", svc->name);
    return 0;
}

static int tcp_ctrl_worker(void *arg)
{
    struct tcp_service *svc = arg;
    struct msghdr msg;
    struct kvec kvec[2];
    struct rfnm_tcp_ctrl_header {
        uint8_t cmd;
        uint16_t size;
    } __packed header;
    uint8_t buf[RFNM_SYSCTL_TRANSFER_SIZE];
    int ret;
    struct socket *sock = NULL;
    DEFINE_WAIT(wait);

    pr_info("TCP control worker started\n");
    svc->worker_running = true;

    while (svc->running) {
        /* Get socket with lock */
        mutex_lock(&svc->conn_lock);
        if (svc->dead_sock) {
            /* this worker is the only I/O user and is between commands here,
             * so the retired connection is safe to free */
            sock_release(svc->dead_sock);
            svc->dead_sock = NULL;
        }
        sock = svc->conn_sock;
        if (sock)
            sock_hold(sock->sk);  /* Increase reference count */
        mutex_unlock(&svc->conn_lock);

        if (!sock) {
            /* Wait for new connection */
            prepare_to_wait(&svc->conn_wait, &wait, TASK_INTERRUPTIBLE);
            if (!svc->conn_sock && svc->running)
                schedule_timeout(msecs_to_jiffies(100));
            finish_wait(&svc->conn_wait, &wait);
            continue;
        }

        /* Read header first */
        memset(&msg, 0, sizeof(msg));
        kvec[0].iov_base = &header;
        kvec[0].iov_len = sizeof(header);

        ret = kernel_recvmsg(sock, &msg, kvec, 1,
                            sizeof(header), MSG_WAITALL);
        
        if (ret != sizeof(header)) {
            if (ret == 0) {
                pr_info("TCP control connection closed\n");
            } else if (ret > 0) {
                pr_err("TCP control partial header: %d bytes\n", ret);
            } else if (ret != -ECONNRESET && ret != -EPIPE) {
                pr_err("TCP control receive error: %d\n", ret);
            }
            
            sock_put(sock->sk);  /* Release reference */

            /* Detach and free if it's still the same socket; if a closer
             * already retired it, the loop top reaps it via dead_sock */
            mutex_lock(&svc->conn_lock);
            if (svc->conn_sock == sock) {
                svc->conn_sock = NULL;
                mutex_unlock(&svc->conn_lock);
                sock_release(sock);
            } else {
                mutex_unlock(&svc->conn_lock);
            }
            continue;
        }

        /* Validate header */
        if (header.size > RFNM_SYSCTL_TRANSFER_SIZE) {
            pr_err("TCP control invalid size: %u\n", header.size);
            sock_put(sock->sk);
            continue;
        }

        /* Read payload if any */
        if (header.size > 0) {
            kvec[0].iov_base = buf;
            kvec[0].iov_len = header.size;

            ret = kernel_recvmsg(sock, &msg, kvec, 1,
                                header.size, MSG_WAITALL);
            
            if (ret != header.size) {
                pr_err("TCP control incomplete payload: %d/%u bytes\n", 
                       ret, header.size);
                sock_put(sock->sk);
                continue;
            }
        }

        /* Process command */
        uint32_t response_size = 0;

        if((header.cmd & 0xff) != 0x06)
            pr_debug("TCP control cmd %d\n", header.cmd & 0xff);
        
        if (rfnm_dev_process_udp_ctrl(header.cmd, &response_size, buf)) {
            /* Send response header + data */
            struct rfnm_tcp_ctrl_header resp_header = {
                .cmd = header.cmd,
                .size = response_size
            };
            
            kvec[0].iov_base = &resp_header;
            kvec[0].iov_len = sizeof(resp_header);
            kvec[1].iov_base = buf;
            kvec[1].iov_len = response_size;
            
            mutex_lock(&svc->send_lock);
            ret = kernel_sendmsg(sock, &msg, kvec, 
                                response_size > 0 ? 2 : 1, 
                                sizeof(resp_header) + response_size);
            mutex_unlock(&svc->send_lock);
            
            if (ret < 0) {
                pr_err("TCP control send error: %d\n", ret);
            }
        }
        
        sock_put(sock->sk);  /* Release reference */
    }

    svc->worker_running = false;
    complete(&svc->worker_done);
    pr_info("TCP control worker exiting\n");
    return 0;
}

/* TCP data worker */
/* Per-worker socket tracking: each data worker keeps its own held reference to the
 * current connection. When the connection is retired (dead_sock), the LAST worker to
 * notice releases the socket struct - refcounted via dead_users so the two direction
 * workers can share one full-duplex TCP connection without a lifecycle race. */
static void tcp_worker_track_sock(struct tcp_service *svc, struct socket **sockp)
{
    mutex_lock(&svc->conn_lock);
    if (*sockp != svc->conn_sock) {
        if (*sockp) {
            sock_put((*sockp)->sk);
            if (svc->dead_sock == *sockp && atomic_dec_and_test(&svc->dead_users)) {
                sock_release(svc->dead_sock);
                svc->dead_sock = NULL;
            }
        }
        *sockp = svc->conn_sock;
        if (*sockp) {
            sock_hold((*sockp)->sk);
        }
    }
    mutex_unlock(&svc->conn_lock);
}

/* board -> host: drain the local RX pool into coalesced sends. One direction only;
 * runs concurrently with the ingest worker on the same (full-duplex) socket. */
static int tcp_data_tx_worker(void *arg)
{
    struct tcp_service *svc = arg;
    int ret;
    bool connected = false;
    struct socket *sock = NULL;
    DEFINE_WAIT(wait);

    pr_info("TCP data TX worker started\n");
    svc->worker2_running = true;

    while (svc->running) {
        tcp_worker_track_sock(svc, &sock);

        if (!sock) {
            if (connected) {
                pr_info("TCP data TX: connection lost\n");
                connected = false;
            }
            prepare_to_wait(&svc->conn_wait, &wait, TASK_INTERRUPTIBLE);
            if (!svc->conn_sock && svc->running)
                schedule_timeout(msecs_to_jiffies(100));
            finish_wait(&svc->conn_wait, &wait);
            continue;
        }
        connected = true;

        {
            int drained;

            for (drained = 0; drained < 16; ) {
                void *handles[RFNM_ETH_TX_BATCH];
                struct kvec batch[RFNM_ETH_TX_BATCH];
                struct msghdr dmsg;
                uint8_t *pkt;
                int nbuf = 0, total, sent = 0, i;

                while (nbuf < RFNM_ETH_TX_BATCH &&
                       (handles[nbuf] = rfnm_dequeue_local_buffer_rx_ref(&pkt)) != NULL) {
                    struct rfnm_rx_usb_buf *rb = (struct rfnm_rx_usb_buf *)pkt;

                    /* r6-eth v2 framing: send the head + exactly the payload the head
                     * declares - elem_cnt elements of rb->fmt (PACKED12 3 B, CS16 4 B).
                     * Fixed records floored the host's RX latency at the 80-slot fill
                     * and made every partial/early-ship lever TCP-dead; the TX direction
                     * always framed variably. Shipping BOTH formats is the fix: the
                     * old PACKED12-only filter silently starved every remote reader
                     * whenever a local session owned the pool format - a fmt the reader
                     * doesn't expect must cost wire bytes, never silence. Only a head
                     * violating the protocol itself is dropped, and loudly. */
                    if ((rb->fmt != RFNM_PACKET_FMT_PACKED12 && rb->fmt != RFNM_PACKET_FMT_CS16) ||
                        rb->elem_cnt == 0 || rb->elem_cnt > RFNM_USB_RX_PACKET_ELEM_CNT) {
                        pr_err_ratelimited("rfnm_eth: dropping RX record with invalid head (fmt %u elem_cnt %u)\n",
                                           rb->fmt, rb->elem_cnt);
                        rfnm_release_local_buffer_rx_ref(handles[nbuf]);
                        continue;
                    }
                    batch[nbuf].iov_base = pkt;
                    batch[nbuf].iov_len = RFNM_USB_RX_PACKET_HEAD_SIZE +
                        (size_t)rb->elem_cnt * (rb->fmt == RFNM_PACKET_FMT_CS16 ? 4 : 3);
                    nbuf++;
                }
                if (nbuf == 0)
                    break;
                drained += nbuf;
                total = 0;
                for (i = 0; i < nbuf; i++)
                    total += batch[i].iov_len;

                memset(&dmsg, 0, sizeof(dmsg));
                mutex_lock(&svc->send_lock);
                while (sent < total) {
                    struct kvec kv[RFNM_ETH_TX_BATCH];
                    int nkv = 0, skip = sent;

                    for (i = 0; i < nbuf; i++) {
                        if (skip >= batch[i].iov_len) {
                            skip -= batch[i].iov_len;
                            continue;
                        }
                        kv[nkv].iov_base = (uint8_t *)batch[i].iov_base + skip;
                        kv[nkv].iov_len = batch[i].iov_len - skip;
                        skip = 0;
                        nkv++;
                    }

                    ret = kernel_sendmsg(sock, &dmsg, kv, nkv, total - sent);
                    if (ret <= 0)
                        break;
                    sent += ret;
                }
                mutex_unlock(&svc->send_lock);

                for (i = 0; i < nbuf; i++)
                    rfnm_release_local_buffer_rx_ref(handles[i]);

                if (sent != total) {
                    if (ret >= 0) {
                        pr_err_ratelimited("TCP data short send: %d of %d bytes\n", sent, total);
                    } else if (ret != -EPIPE && ret != -ECONNRESET) {
                        pr_err_ratelimited("TCP data TX error: %d\n", ret);
                    }
                    break;
                }
            }

            if (drained == 0) {
                wait_event_interruptible_timeout(local_rx_poll,
                                                 rfnm_local_buffer_rx_available(),
                                                 usecs_to_jiffies(1000));
            }
        }
    }

    if (sock) {
        sock_put(sock->sk);
    }
    svc->worker2_running = false;
    complete(&svc->worker2_done);
    pr_info("TCP data TX worker exiting\n");
    return 0;
}

/* host -> board ingest. The original single worker (below, now this) keeps the recv
 * side; the send side moved to tcp_data_tx_worker so neither direction waits on the
 * other - at 61.44M duplex the combined path was one CPU-bound thread.
 *
 * Frames are recv'd in two stages: the fixed-size header into a small parse buffer,
 * then the payload straight into a claimed pool slot - the old staging buffer cost a
 * full extra copy of the TX stream plus a leftover memmove per frame. */
static int tcp_data_worker(void *arg)
{
    struct tcp_service *svc = arg;
    struct msghdr msg;
    struct kvec kvec;
    uint8_t hdr_buf[RFNM_USB_TX_PACKET_HEAD_SIZE];
    uint32_t hdr_have = 0;
    void *slot_handle = NULL;
    uint8_t *slot = NULL;
    uint32_t frame_need = 0, frame_have = 0;
    int ret = 0;
    bool connected = false;
    bool pool_full = false;
    struct socket *sock = NULL;
    DEFINE_WAIT(wait);

    pr_info("TCP data worker started\n");
    svc->worker_running = true;

    pr_info("Kernel expecting packet sizes: TX=%d, RX=%d\n",
            RFNM_USB_TX_PACKET_SIZE, RFNM_USB_RX_PACKET_SIZE);

    while (svc->running) {
        struct socket *prev_sock = sock;

        tcp_worker_track_sock(svc, &sock);

        if (sock != prev_sock) {
            /* new (or no) connection: the byte stream restarted, drop parse state */
            hdr_have = 0;
            if (slot_handle) {
                rfnm_abort_local_buffer_tx(slot_handle);
                slot_handle = NULL;
                slot = NULL;
            }
        }

        if (!sock) {
            if (connected) {
                pr_info("TCP data connection lost\n");
                connected = false;
            }
            /* Wait for new connection */
            prepare_to_wait(&svc->conn_wait, &wait, TASK_INTERRUPTIBLE);
            if (!svc->conn_sock && svc->running)
                schedule_timeout(msecs_to_jiffies(100));
            finish_wait(&svc->conn_wait, &wait);
            continue;
        }

        if (!connected) {
            pr_info("TCP data connection established\n");
            connected = true;
        }

        pool_full = false;

        /* drain whatever TCP has buffered: header -> claim slot -> payload -> commit,
         * looping frames until the socket runs dry or the pool backpressures */
        for (;;) {
            if (!slot_handle) {
                if (hdr_have < RFNM_USB_TX_PACKET_HEAD_SIZE) {
                    memset(&msg, 0, sizeof(msg));
                    kvec.iov_base = hdr_buf + hdr_have;
                    kvec.iov_len = RFNM_USB_TX_PACKET_HEAD_SIZE - hdr_have;
                    ret = kernel_recvmsg(sock, &msg, &kvec, 1, kvec.iov_len, MSG_DONTWAIT);
                    if (ret <= 0) {
                        break;
                    }
                    hdr_have += ret;
                    if (hdr_have < RFNM_USB_TX_PACKET_HEAD_SIZE) {
                        continue;
                    }
                }

                {
                    struct rfnm_tx_usb_buf *hdr = (struct rfnm_tx_usb_buf *)hdr_buf;

                    /* TCP is a reliable byte stream, so framing only breaks on a
                     * protocol bug - treat a bad magic/multi as fatal for the frame */
                    if (hdr->magic != 0x758f4d4a || hdr->multi < 1 || hdr->multi > RFNM_TX_USB_BUF_MULTI) {
                        pr_err_ratelimited("TCP TX framing lost (magic %08x multi %u), dropping header\n",
                                           hdr->magic, hdr->multi);
                        hdr_have = 0;
                        continue;
                    }
                    frame_need = RFNM_USB_TX_PACKET_HEAD_SIZE + hdr->multi * LA_TX_BASE_BUFSIZE_12;
                }

                slot_handle = rfnm_claim_local_buffer_tx(&slot);
                if (!slot_handle) {
                    pool_full = true;
                    break;
                }
                memcpy(slot, hdr_buf, RFNM_USB_TX_PACKET_HEAD_SIZE);
                frame_have = RFNM_USB_TX_PACKET_HEAD_SIZE;
                hdr_have = 0;
            }

            memset(&msg, 0, sizeof(msg));
            kvec.iov_base = slot + frame_have;
            kvec.iov_len = frame_need - frame_have;
            ret = kernel_recvmsg(sock, &msg, &kvec, 1, kvec.iov_len, MSG_DONTWAIT);
            if (ret <= 0) {
                break;
            }
            frame_have += ret;
            if (frame_have == frame_need) {
                rfnm_commit_local_buffer_tx(slot_handle);
                slot_handle = NULL;
                slot = NULL;
            }
        }

        if (ret == 0) {
            pr_info("TCP data connection closed by peer\n");
            if (slot_handle) {
                rfnm_abort_local_buffer_tx(slot_handle);
                slot_handle = NULL;
                slot = NULL;
            }
            hdr_have = 0;
            mutex_lock(&svc->conn_lock);
            if (svc->conn_sock == sock) {
                /* retire it: both direction workers must drop their references
                 * before the socket struct can be released */
                svc->conn_sock = NULL;
                if (!svc->dead_sock) {
                    svc->dead_sock = sock;
                    atomic_set(&svc->dead_users, 2);
                }
            }
            mutex_unlock(&svc->conn_lock);
            sock_put(sock->sk);
            if (svc->dead_sock == sock && atomic_dec_and_test(&svc->dead_users)) {
                mutex_lock(&svc->conn_lock);
                sock_release(svc->dead_sock);
                svc->dead_sock = NULL;
                mutex_unlock(&svc->conn_lock);
            }
            sock = NULL;
            continue;
        } else if (ret < 0 && ret != -EAGAIN && ret != -EWOULDBLOCK && ret != -ECONNRESET) {
            pr_err("TCP data RX error: %d\n", ret);
        }

        /* board->host sends live in tcp_data_tx_worker now; this thread only
         * ingests. Two park reasons: no socket bytes (wake = data-ready hook on
         * eth_ingest_wq), or waiting on a pool slot for a parsed header (wake =
         * pool recycler on local_tx_poll, edge-triggered). Spinning on a full
         * pool while the socket had bytes queued was a 100% CPU busy-loop that
         * starved the whole stack. */
        if (pool_full) {
            wait_event_interruptible_timeout(local_tx_poll, rfnm_local_buffer_tx_free(), usecs_to_jiffies(500));
        } else {
            wait_event_interruptible_timeout(eth_ingest_wq,
                                             (sock && !skb_queue_empty(&sock->sk->sk_receive_queue)),
                                             usecs_to_jiffies(1000));
        }
    }

    /* Release final socket reference and any half-filled slot */
    if (slot_handle) {
        rfnm_abort_local_buffer_tx(slot_handle);
    }
    if (sock) {
        sock_put(sock->sk);
    }

    svc->worker_running = false;
    complete(&svc->worker_done);
    pr_info("TCP data worker exiting\n");
    return 0;
}

static int create_tcp_service(struct tcp_service *svc, const char *name,
                             int port, int (*worker_fn)(void *),
                             int (*worker2_fn)(void *))
{
    int ret;

    memset(svc, 0, sizeof(*svc));
    svc->name = name;
    svc->port = port;
    mutex_init(&svc->conn_lock);
    mutex_init(&svc->send_lock);
    init_completion(&svc->worker_done);
    init_waitqueue_head(&svc->conn_wait);

    /* Create listening socket */
    ret = sock_create_kern(&init_net, AF_INET, SOCK_STREAM,
                          IPPROTO_TCP, &svc->listen_sock);
    if (ret < 0) {
        pr_err("Failed to create TCP socket for %s: %d\n", name, ret);
        return ret;
    }

    /* Allow address reuse */
    sock_set_reuseaddr(svc->listen_sock->sk);

    /* Bind to specified port */
    memset(&svc->bind_addr, 0, sizeof(svc->bind_addr));
    svc->bind_addr.sin_family = AF_INET;
    svc->bind_addr.sin_port = htons(port);
    
    ret = in4_pton(local_ip, strlen(local_ip),
                   (u8 *)&svc->bind_addr.sin_addr.s_addr, -1, NULL);
    if (!ret) {
        pr_err("Invalid IP address: %s\n", local_ip);
        goto err_close;
    }

    ret = kernel_bind(svc->listen_sock, (struct sockaddr *)&svc->bind_addr,
                     sizeof(svc->bind_addr));
    if (ret < 0) {
        pr_err("Failed to bind TCP %s to %s:%d: %d\n", 
               name, local_ip, port, ret);
        goto err_close;
    }

    /* Start listening */
    ret = kernel_listen(svc->listen_sock, 1);
    if (ret < 0) {
        pr_err("Failed to listen on TCP %s: %d\n", name, ret);
        goto err_close;
    }

    /* wake the accept thread on a completed handshake (see tcp_listen_ready_wake) */
    svc->listen_sock->sk->sk_user_data = svc;
    svc->listen_orig_data_ready = svc->listen_sock->sk->sk_data_ready;
    smp_wmb();
    svc->listen_sock->sk->sk_data_ready = tcp_listen_ready_wake;

    /* Start threads */
    svc->running = true;
    
    svc->worker_thread = kthread_run(worker_fn, svc, "rfnm_%s_work", name);
    if (IS_ERR(svc->worker_thread)) {
        ret = PTR_ERR(svc->worker_thread);
        pr_err("Failed to create %s worker thread: %d\n", name, ret);
        goto err_close;
    }

    if (worker2_fn) {
        init_completion(&svc->worker2_done);
        svc->worker2_thread = kthread_run(worker2_fn, svc, "rfnm_%s_wrk2", name);
        if (IS_ERR(svc->worker2_thread)) {
            ret = PTR_ERR(svc->worker2_thread);
            svc->worker2_thread = NULL;
            pr_err("Failed to create %s worker2 thread: %d\n", name, ret);
            svc->running = false;
            kthread_stop(svc->worker_thread);
            wait_for_completion(&svc->worker_done);
            goto err_close;
        }
    }

    svc->accept_thread = kthread_run(tcp_accept_thread, svc, "rfnm_%s_acc", name);
    if (IS_ERR(svc->accept_thread)) {
        ret = PTR_ERR(svc->accept_thread);
        pr_err("Failed to start %s accept thread: %d\n", name, ret);
        svc->running = false;
        kthread_stop(svc->worker_thread);
        wait_for_completion(&svc->worker_done);
        goto err_close;
    }

    pr_info("TCP %s listening on %s:%d\n", name, local_ip, port);
    return 0;

err_close:
    sock_release(svc->listen_sock);
    svc->listen_sock = NULL;
    return ret;
}

static void destroy_tcp_service(struct tcp_service *svc)
{
    if (!svc->listen_sock)
        return;

    pr_info("Stopping TCP %s service\n", svc->name);

    svc->running = false;

    /* Close any active connection */
    close_connection_safe(svc);

    /* Wake up any waiting threads */
    wake_up(&svc->conn_wait);

    /* Stop accept thread first */
    if (svc->accept_thread && !IS_ERR(svc->accept_thread)) {
        kthread_stop(svc->accept_thread);
        svc->accept_thread = NULL;
    }
    
    /* Stop the second (data TX) worker first: same protocol as the primary */
    if (svc->worker2_thread && !IS_ERR(svc->worker2_thread)) {
        if (svc->worker2_running) {
            wake_up(&svc->conn_wait);
            wake_up(&local_rx_poll);
            wake_up(&local_tx_poll);
            wake_up(&eth_ingest_wq);
            if (!wait_for_completion_timeout(&svc->worker2_done, msecs_to_jiffies(5000))) {
                pr_warn("%s worker2 thread did not exit cleanly\n", svc->name);
                kthread_stop(svc->worker2_thread);
            }
        }
        svc->worker2_thread = NULL;
    }

    /* Stop worker thread */
    if (svc->worker_thread && !IS_ERR(svc->worker_thread)) {
        /* Check if worker is still running before trying to wake it */
        if (svc->worker_running) {
            /* Wake up the worker if it might be sleeping */
            wake_up(&svc->conn_wait);
            
            /* Wait for worker to complete */
            if (wait_for_completion_timeout(&svc->worker_done, 
                                          msecs_to_jiffies(5000))) {
                /* Worker completed normally */
                svc->worker_thread = NULL;
            } else {
                /* Timeout - force stop */
                pr_warn("%s worker thread did not exit cleanly\n", svc->name);
                kthread_stop(svc->worker_thread);
                svc->worker_thread = NULL;
            }
        } else {
            /* Worker already exited, just clean up */
            svc->worker_thread = NULL;
        }
    }

    /* Workers are gone; drain any remaining connection sockets */
    if (svc->dead_sock) {
        sock_release(svc->dead_sock);
        svc->dead_sock = NULL;
    }
    if (svc->conn_sock) {
        sock_release(svc->conn_sock);
        svc->conn_sock = NULL;
    }

    /* Close listening socket */
    if (svc->listen_sock) {
        kernel_sock_shutdown(svc->listen_sock, SHUT_RDWR);
        sock_release(svc->listen_sock);
        svc->listen_sock = NULL;
    }

    mutex_destroy(&svc->conn_lock);
    mutex_destroy(&svc->send_lock);
}

/* ------------------------------------------------------------------ */
/* Module init/exit */

static int __init rfnm_tcp_init(void)
{
    int ret;

    pr_info("RFNM TCP/UDP module loading (binding to %s)\n", local_ip);

    /* Start UDP discovery service */
    ret = init_udp_discovery();
    if (ret) {
        pr_err("Failed to initialize UDP discovery: %d\n", ret);
        return ret;
    }

    /* Start TCP control service */
    ret = create_tcp_service(&tcp_ctrl, "ctrl", RFNM_TCP_CTRL_PORT, tcp_ctrl_worker, NULL);
    if (ret) {
        pr_err("Failed to create TCP control service: %d\n", ret);
        goto err_disc;
    }

    /* Start TCP data service */
    ret = create_tcp_service(&tcp_data, "data", RFNM_TCP_DATA_PORT, tcp_data_worker, tcp_data_tx_worker);
    if (ret) {
        pr_err("Failed to create TCP data service: %d\n", ret);
        goto err_ctrl;
    }

    pr_info("RFNM TCP/UDP module loaded successfully\n");
    return 0;

err_ctrl:
    destroy_tcp_service(&tcp_ctrl);
err_disc:
    exit_udp_discovery();
    return ret;
}

static void __exit rfnm_tcp_exit(void)
{
    pr_info("RFNM TCP/UDP module unloading\n");

    destroy_tcp_service(&tcp_data);
    destroy_tcp_service(&tcp_ctrl);
    exit_udp_discovery();

    pr_info("RFNM TCP/UDP module unloaded\n");
}

module_init(rfnm_tcp_init);
module_exit(rfnm_tcp_exit);

MODULE_DESCRIPTION("RFNM TCP/UDP Network Transport Driver - Fixed");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("RFNM");
MODULE_VERSION("1.2");