#include <libwebsockets.h>
#include <pthread.h>

struct msg {
    void *payload;
    size_t len;
};

struct msg first_message;

/*
 * Each client will have an instance of this
 * (this is not sent to the client)
 * we get the one corresponding to the current client as parameter
 * (void *user)
 */
struct per_session_data__minimal {
    struct per_session_data__minimal *pss_list_next;
    struct lws *wsi;
    uint32_t tail;
    char loaded;
};

/*
 * Same but for vhost, we will have only one host
 * normally...
 */
struct per_vhost_data__minimal {
    struct lws_context *context;
    struct lws_vhost *vhost;
    const struct lws_protocols *protocol;

    struct per_session_data__minimal *pss_list;
    pthread_mutex_t lock_ring;
    pthread_t pthread;
    struct lws_ring *ring;

    const char *config;
    char finished;
};

/*
 * Destroy msg
 */
static void __destroy_message(void *_msg) {
    struct msg *msg = _msg;

    free(msg->payload);
    msg->payload = NULL;
    msg->len = 0;
}

/*
 * The data that we send
 * id is the index (correspond to index in the order we got
 * it in argv)
 * and 3 floats for position in space
 */
struct vector3 {
    int id;
    float x, y, z;
};

/*
 * This thread asks the user...
 *
 * to send data we dont do it directly
 * we put them on a buffer and call lws_cancel_service
 * which will cause the my_callback to be called on the
 * main thread with the reason parameter equals to
 * LWS_CALLBACK_EVENT_WAIT_CANCELLED
 *
 * it uses a ring buffer locked by a mutex
 *
 */
static void *thread_prompt(void *d) {
    struct per_vhost_data__minimal *vhd =
            (struct per_vhost_data__minimal *) d;
    struct msg amsg;
    struct vector3 vect;
    int n;


    do {
        scanf("%d %f %f %f", &vect.id, &vect.x, &vect.y, &vect.z);
        printf("The message is %d %f %f %f\n", vect.id, vect.x, vect.y, vect.z);

        size_t size = sizeof(struct vector3);
        amsg.payload = malloc(size + LWS_PRE);
        memcpy(amsg.payload + LWS_PRE, &vect, size);
        amsg.len = size;

        pthread_mutex_lock(&vhd->lock_ring);
        if (lws_ring_insert(vhd->ring, &amsg, 1) != 1) {
            __destroy_message(&amsg);
            lwsl_user("Drop\n");
        } else lws_cancel_service(vhd->context);

        pthread_mutex_unlock(&vhd->lock_ring);
    } while (!vhd->finished);

    lwsl_notice("thread finished");
    pthread_exit(NULL);
}

/*
 * The protocol called "my-protocol" is this
 * function (state machine).
 *
 * LWS_CALLBACK_PROTOCOL_INIT and LWS_CALLBACK_PROTOCOL_DESTROY
 * happens one time per protocol on a certain vhost
 *
 * LWS_CALLBACK_ESTABLISHED and LWS_CALLBACK_CLOSED
 * one per websocket connection
 *
 * We can't write to the client as we want,
 * we should wait the LWS_CALLBACK_SERVER_WRITEABLE reason
 * because libwebsocket does not waste any buffer
 * locally it will wait for the client to have enough
 * buffer (I think it uses tcp ack, and window size
 * to know all this)
 *
 * when we need to write something to the client
 * we can make my_callback to be called with
 * LWS_CALLBACK_SERVER_WRITEABLE reason by
 * calling lws_callback_on_writable (as we do
 * in LWS_CALLBACK_ESTABLISHED)
 */
static int my_callback(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len) {
    struct per_session_data__minimal *pss = (struct per_session_data__minimal *) user;
    struct per_vhost_data__minimal *vhd = (struct per_vhost_data__minimal *)
            lws_protocol_vh_priv_get(lws_get_vhost(wsi), lws_get_protocol(wsi));
    int m;
    void *retval;
    const struct msg *pmsg;

    printf("raison %d init=%d; established=%d\n", reason, LWS_CALLBACK_PROTOCOL_INIT, LWS_CALLBACK_ESTABLISHED);

    switch (reason) {
        case LWS_CALLBACK_PROTOCOL_INIT:
            vhd = lws_protocol_vh_priv_zalloc(
                    lws_get_vhost(wsi),
                    lws_get_protocol(wsi),
                    sizeof(struct per_vhost_data__minimal));
            pthread_mutex_init(&vhd->lock_ring, NULL);

            vhd->context = lws_get_context(wsi);
            vhd->protocol = lws_get_protocol(wsi);
            vhd->vhost = lws_get_vhost(wsi);
            vhd->ring = lws_ring_create(sizeof(struct msg), 8, __destroy_message);
            if (!vhd->ring) {
                lwsl_err("%s: failed to create ring\n", __func__);
                return 1;
            }
            if (!pthread_create(&vhd->pthread, NULL, thread_prompt, vhd)) {
                break;
            }
            lwsl_user("WTF");
        case LWS_CALLBACK_PROTOCOL_DESTROY:
            vhd->finished = 1;
            pthread_join(vhd->pthread, &retval);
            pthread_mutex_destroy(&vhd->lock_ring);
            break;
        case LWS_CALLBACK_ESTABLISHED: lws_ll_fwd_insert(pss, pss_list_next, vhd->pss_list);
            pss->wsi = wsi;
            pss->tail = lws_ring_get_oldest_tail(vhd->ring);
            lwsl_user("ESTABLISHED\n");
            lws_callback_on_writable(wsi);
            break;
        case LWS_CALLBACK_CLOSED: lws_ll_fwd_remove(struct per_session_data__minimal, pss_list_next, pss,
                                                    vhd->pss_list);
            break;
        case LWS_CALLBACK_SERVER_WRITEABLE:
            lwsl_user("Writing msg\n");
            // Is it the first message?
            if (!pss->loaded) {
                lwsl_user("First msg\n");
                /* We should always keep LWS_PRE bytes free
                 * but we give the pointer pointing to the first
                 * byte after thoses free bytes
                 * */
                unsigned char buffer[first_message.len + LWS_PRE];
                memcpy(buffer + LWS_PRE, first_message.payload, first_message.len);
                lws_write(wsi, buffer + LWS_PRE, first_message.len, LWS_WRITE_TEXT);
                pss->loaded = 1;
            } else {

                pthread_mutex_lock(&vhd->lock_ring);
                pmsg = lws_ring_get_element(vhd->ring, &pss->tail);

                // We have something to send?
                if (pmsg) {
                    m = lws_write(wsi, ((
                            unsigned char *) pmsg->payload) + LWS_PRE, pmsg->len, LWS_WRITE_BINARY);
                    if (m < (int) pmsg->len) {
                        pthread_mutex_unlock(&vhd->lock_ring);
                        lwsl_err("ERROR %d writing socket", m);
                        return -1;
                    }
                    lws_ring_consume_and_update_oldest_tail(
                            vhd->ring,
                            struct per_session_data__minimal,
                            &pss->tail,
                            1,
                            vhd->pss_list,
                            tail,
                            pss_list_next
                    );
                    // We still have more to send?
                    if (lws_ring_get_element(vhd->ring, &pss->tail)) lws_callback_on_writable(pss->wsi);
                }
                pthread_mutex_unlock(&vhd->lock_ring);
            }
            break;
        case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
            // The prompt thread want us to send something
            if (!vhd) break;
            lws_start_foreach_llp(struct per_session_data__minimal**, ppss, vhd->pss_list)
                    {
                        // We want callback to be called
                        // with LWS_CALLBACK_SERVER_WRITEABLE
                        // reason on all clients
                        lws_callback_on_writable((*ppss)->wsi);
                    }
            lws_end_foreach_llp(ppss, pss_list_next);
            break;
        default:
            break;

    }
    return 0;
}

/*
 * This struct defines the protocol
 * on the browser when we create a websocket object
 * "spawn-objects" should be given as second parameter
 * (subprotocol).
 * */
#define MY_PROTOCOL \
{\
"spawn-objects", \
my_callback, \
sizeof(struct per_session_data__minimal), \
1024, \
0, NULL, 0 \
}
