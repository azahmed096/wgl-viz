#include <libwebsockets.h>
#include <stdio.h>
#include "my_protocol.h"


/*
 * Protocols:
 *  if the client does not specify a protocol,
 *  it will take the first one
*/
static struct lws_protocols protocols[] = {
        {"http", lws_callback_http_dummy, 0, 0},
        MY_PROTOCOL,
        {NULL, NULL, 0, 0}
};

static int interrupted;

static const struct lws_protocol_vhost_options app = {
        NULL, NULL, ".glb", "application/x-binary"};

static const struct lws_http_mount mount = {
        /* .mount_next*/           NULL,        /* linked-list "next" */
        /* .mountpoint */        "/",        /* mountpoint URL */
        /* .origin */        "./mount-origin",    /* serve from dir */
        /* .def */            "index.html",    /* default filename */
        /* .protocol */            NULL,
        /* .cgienv */            NULL,
        /* .extra_mimetypes */    &app,
        /* .interpret */        NULL,
        /* .cgi_timeout */        0,
        /* .cache_max_age */        0,
        /* .auth_mask */        0,
        /* .cache_reusable */        0,
        /* .cache_revalidate */        0,
        /* .cache_intermediaries */    0,
        /* .origin_protocol */        LWSMPRO_FILE,    /* files in a dir */
        /* .mountpoint_len */        1,        /* char count */
        /* .basic_auth_login_file */    NULL,
};

void sigint_handler(int sig) {
    interrupted = 1;
}

const char *help = "Usage: <host> <port> <models/model1> ...\n";
char files[4096];

int main(int argc, const char **argv) {
    struct lws_context_creation_info info;
    struct lws_context *context;
    int n = 0;
    char *_unused;
    memset(&info, 0, sizeof info);

    // ARGS
    if (argc < 3) {
        puts(help);
        return -1;
    } else {
        info.vhost_name = argv[1];
        info.port = (int) strtol(argv[2], &_unused, 10);
    }
    if (argc > 3) {
        strcpy(files, argv[3]);
        for (int i = 4; i < argc; ++i) {
            // SURE NOT MOST EFFICIENT WAY...
            strcat(files, "|");
            strcat(files, argv[i]);
        }
    }

    /*
     * The first message will serve to send download paths
     * of models to the client */
    first_message.payload = files;
    first_message.len = strlen(files);


    signal(SIGINT, sigint_handler);

    // Log level
    int logs = LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE;
    lws_set_log_level(logs, NULL);

    // Informations
    info.mounts = &mount;
    info.protocols = protocols;
    info.ws_ping_pong_interval = 10;
    context = lws_create_context(&info);
    if (!context) {
        lwsl_err("lws init failed\n");
        return 1;
    }

    while (n >= 0 && !interrupted) {
        n = lws_service(context, 1000);
    }

    lws_context_destroy(context);

    return 0;
}
