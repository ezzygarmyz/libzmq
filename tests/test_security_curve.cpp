/*
    Copyright (c) 2007-2017 Contributors as noted in the AUTHORS file

    This file is part of libzmq, the ZeroMQ core engine in C++.

    libzmq is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License (LGPL) as published
    by the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    As a special exception, the Contributors give you permission to link
    this library with independent modules to produce an executable,
    regardless of the license terms of these independent modules, and to
    copy and distribute the resulting executable under terms of your choice,
    provided that you also meet, for each linked independent module, the
    terms and conditions of the license of that module. An independent
    module is a module which is not derived from or based on this library.
    If you modify this library, you must extend this exception to your
    version of the library.

    libzmq is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
    License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "testutil.hpp"
#if defined (ZMQ_HAVE_WINDOWS)
#   include <winsock2.h>
#   include <ws2tcpip.h>
#   include <stdexcept>
#   define close closesocket
#else
#   include <sys/socket.h>
#   include <netinet/in.h>
#   include <arpa/inet.h>
#   include <unistd.h>
#endif

//  We'll generate random test keys at startup
static char client_public [41];
static char client_secret [41];
static char server_public [41];
static char server_secret [41];

#ifdef ZMQ_BUILD_DRAFT_API
//  Read one event off the monitor socket; return value and address
//  by reference, if not null, and event number by value. Returns -1
//  in case of error.

static int
get_monitor_event (void *monitor, int *value, char **address, int recv_flag)
{
    //  First frame in message contains event number and value
    zmq_msg_t msg;
    zmq_msg_init (&msg);
    if (zmq_msg_recv (&msg, monitor, recv_flag) == -1)
        return -1;              //  Interruped, presumably
    assert (zmq_msg_more (&msg));

    uint8_t *data = (uint8_t *) zmq_msg_data (&msg);
    uint16_t event = *(uint16_t *) (data);
    if (value)
        *value = *(uint32_t *) (data + 2);

    //  Second frame in message contains event address
    zmq_msg_init (&msg);
    if (zmq_msg_recv (&msg, monitor, recv_flag) == -1)
        return -1;              //  Interruped, presumably
    assert (!zmq_msg_more (&msg));

    if (address) {
        uint8_t *data = (uint8_t *) zmq_msg_data (&msg);
        size_t size = zmq_msg_size (&msg);
        *address = (char *) malloc (size + 1);
        memcpy (*address, data, size);
        *address [size] = 0;
    }
    return event;
}
#endif


//  --------------------------------------------------------------------------
//  This methods receives and validates ZAP requestes (allowing or denying
//  each client connection).

static void zap_handler (void *handler)
{
    //  Process ZAP requests forever
    while (true) {
        char *version = s_recv (handler);
        if (!version)
            break;          //  Terminating

        char *sequence = s_recv (handler);
        char *domain = s_recv (handler);
        char *address = s_recv (handler);
        char *identity = s_recv (handler);
        char *mechanism = s_recv (handler);
        uint8_t client_key [32];
        int size = zmq_recv (handler, client_key, 32, 0);
        assert (size == 32);

        char client_key_text [41];
        zmq_z85_encode (client_key_text, client_key, 32);

        assert (streq (version, "1.0"));
        assert (streq (mechanism, "CURVE"));
        assert (streq (identity, "IDENT"));

        s_sendmore (handler, version);
        s_sendmore (handler, sequence);

        if (streq (client_key_text, client_public)) {
            s_sendmore (handler, "200");
            s_sendmore (handler, "OK");
            s_sendmore (handler, "anonymous");
            s_send     (handler, "");
        }
        else {
            s_sendmore (handler, "400");
            s_sendmore (handler, "Invalid client public key");
            s_sendmore (handler, "");
            s_send     (handler, "");
        }
        free (version);
        free (sequence);
        free (domain);
        free (address);
        free (identity);
        free (mechanism);
    }
    zmq_close (handler);
}


int main (void)
{
#ifndef ZMQ_HAVE_CURVE
    printf ("CURVE encryption not installed, skipping test\n");
    return 0;
#endif
    //  Generate new keypairs for this test
    int rc = zmq_curve_keypair (client_public, client_secret);
    assert (rc == 0);
    rc = zmq_curve_keypair (server_public, server_secret);
    assert (rc == 0);

    setup_test_environment ();
    void *ctx = zmq_ctx_new ();
    assert (ctx);

    //  Spawn ZAP handler
    //  We create and bind ZAP socket in main thread to avoid case
    //  where child thread does not start up fast enough.
    void *handler = zmq_socket (ctx, ZMQ_REP);
    assert (handler);
    rc = zmq_bind (handler, "inproc://zeromq.zap.01");
    assert (rc == 0);
    void *zap_thread = zmq_threadstart (&zap_handler, handler);

    //  Server socket will accept connections
    void *server = zmq_socket (ctx, ZMQ_DEALER);
    assert (server);
    //int reconnect = 10000;
    //rc = zmq_setsockopt (server, ZMQ_RECONNECT_IVL, &reconnect, sizeof(int));
    //assert(rc == 0);
    int as_server = 1;
    rc = zmq_setsockopt (server, ZMQ_CURVE_SERVER, &as_server, sizeof (int));
    assert (rc == 0);
    rc = zmq_setsockopt (server, ZMQ_CURVE_SECRETKEY, server_secret, 41);
    assert (rc == 0);
    rc = zmq_setsockopt (server, ZMQ_IDENTITY, "IDENT", 6);
    assert (rc == 0);
    rc = zmq_bind (server, "tcp://127.0.0.1:*");
    assert (rc == 0);

    size_t len = MAX_SOCKET_STRING;
    char my_endpoint[MAX_SOCKET_STRING];
    rc = zmq_getsockopt (server, ZMQ_LAST_ENDPOINT, my_endpoint, &len);
    assert (rc == 0);

#ifdef ZMQ_BUILD_DRAFT_API
    //  Monitor handshake events on the server
    rc = zmq_socket_monitor (server, "inproc://monitor-server",
            ZMQ_EVENT_HANDSHAKE_SUCCEED | ZMQ_EVENT_HANDSHAKE_FAILED_NO_DETAIL |
            ZMQ_EVENT_HANDSHAKE_FAILED_PROTOCOL | ZMQ_EVENT_HANDSHAKE_FAILED_ENCRYPTION);
    assert (rc == 0);

    //  Create socket for collecting monitor events
    void *server_mon = zmq_socket (ctx, ZMQ_PAIR);
    assert (server_mon);

    //  Connect it to the inproc endpoints so they'll get events
    rc = zmq_connect (server_mon, "inproc://monitor-server");
    assert (rc == 0);
#endif

    //  Check CURVE security with valid credentials
    void *client = zmq_socket (ctx, ZMQ_DEALER);
    assert (client);
    rc = zmq_setsockopt (client, ZMQ_CURVE_SERVERKEY, server_public, 41);
    assert (rc == 0);
    rc = zmq_setsockopt (client, ZMQ_CURVE_PUBLICKEY, client_public, 41);
    assert (rc == 0);
    rc = zmq_setsockopt (client, ZMQ_CURVE_SECRETKEY, client_secret, 41);
    assert (rc == 0);
    rc = zmq_connect (client, my_endpoint);
    assert (rc == 0);
    bounce (server, client);
    rc = zmq_close (client);
    assert (rc == 0);

#ifdef ZMQ_BUILD_DRAFT_API
    int event = get_monitor_event (server_mon, NULL, NULL, 0);
    assert (event == ZMQ_EVENT_HANDSHAKE_SUCCEED);

    // This event has to be the last one
    Sleep(250);
    event = get_monitor_event (server_mon, NULL, NULL, ZMQ_DONTWAIT);
    assert (event == -1);
#endif

    //  Check CURVE security with a garbage server key
    //  This will be caught by the curve_server class, not passed to ZAP
    char garbage_key [] = "0000000000000000000000000000000000000000";
    client = zmq_socket (ctx, ZMQ_DEALER);
    assert (client);
    rc = zmq_setsockopt (client, ZMQ_CURVE_SERVERKEY, garbage_key, 41);
    assert (rc == 0);
    rc = zmq_setsockopt (client, ZMQ_CURVE_PUBLICKEY, client_public, 41);
    assert (rc == 0);
    rc = zmq_setsockopt (client, ZMQ_CURVE_SECRETKEY, client_secret, 41);
    assert (rc == 0);
    rc = zmq_connect (client, my_endpoint);
    assert (rc == 0);
    expect_bounce_fail (server, client);
    close_zero_linger (client);

#ifdef ZMQ_BUILD_DRAFT_API
    event = get_monitor_event (server_mon, NULL, NULL, 0);
    assert (event == ZMQ_EVENT_HANDSHAKE_FAILED_ENCRYPTION);
    // Two times because expect_bounce_fail involves two exchanges
    event = get_monitor_event (server_mon, NULL, NULL, 0);
    assert (event == ZMQ_EVENT_HANDSHAKE_FAILED_ENCRYPTION);

    // This event has to be the last one
    event = get_monitor_event(server_mon, NULL, NULL, ZMQ_DONTWAIT);
    assert (event == -1);
#endif

    //  Check CURVE security with a garbage client public key
    //  This will be caught by the curve_server class, not passed to ZAP
    
    // This enables to flush the incoming monitoring events that disturbs the test.
    // Even though the client socket is closed, the server still handles HELLO
    // messages.
    Sleep(250);
    do {
        event = get_monitor_event(server_mon, NULL, NULL, ZMQ_DONTWAIT);
    } while(event != -1);

    client = zmq_socket (ctx, ZMQ_DEALER);
    assert (client);
    rc = zmq_setsockopt (client, ZMQ_CURVE_SERVERKEY, server_public, 41);
    assert (rc == 0);
    rc = zmq_setsockopt (client, ZMQ_CURVE_PUBLICKEY, garbage_key, 41);
    assert (rc == 0);
    rc = zmq_setsockopt (client, ZMQ_CURVE_SECRETKEY, client_secret, 41);
    assert (rc == 0);
    rc = zmq_connect (client, my_endpoint);
    assert (rc == 0);
    expect_bounce_fail (server, client);
    close_zero_linger (client);

#ifdef ZMQ_BUILD_DRAFT_API
    event = get_monitor_event (server_mon, NULL, NULL, 0);
    assert (event == ZMQ_EVENT_HANDSHAKE_FAILED_ENCRYPTION);
    // Two times because expect_bounce_fail involves two exchanges
    event = get_monitor_event (server_mon, NULL, NULL, 0);
    assert (event == ZMQ_EVENT_HANDSHAKE_FAILED_ENCRYPTION);

    // This event has to be the last one
    event = get_monitor_event(server_mon, NULL, NULL, ZMQ_DONTWAIT);
    assert(event == -1);
#endif

    //  Check CURVE security with a garbage client secret key
    //  This will be caught by the curve_server class, not passed to ZAP
    client = zmq_socket (ctx, ZMQ_DEALER);
    assert (client);
    rc = zmq_setsockopt (client, ZMQ_CURVE_SERVERKEY, server_public, 41);
    assert (rc == 0);
    rc = zmq_setsockopt (client, ZMQ_CURVE_PUBLICKEY, client_public, 41);
    assert (rc == 0);
    rc = zmq_setsockopt (client, ZMQ_CURVE_SECRETKEY, garbage_key, 41);
    assert (rc == 0);
    rc = zmq_connect (client, my_endpoint);
    assert (rc == 0);
    expect_bounce_fail (server, client);
    close_zero_linger (client);

#ifdef ZMQ_BUILD_DRAFT_API
    event = get_monitor_event (server_mon, NULL, NULL, 0);
    assert (event == ZMQ_EVENT_HANDSHAKE_FAILED_ENCRYPTION);
    // Two times because expect_bounce_fail involves two exchanges
    event = get_monitor_event (server_mon, NULL, NULL, 0);
    assert (event == ZMQ_EVENT_HANDSHAKE_FAILED_ENCRYPTION);

    // This event has to be the last one
    event = get_monitor_event(server_mon, NULL, NULL, ZMQ_DONTWAIT);
    assert(event == -1);
#endif

    //  Check CURVE security with bogus client credentials
    //  This must be caught by the ZAP handler
    char bogus_public [41];
    char bogus_secret [41];
    zmq_curve_keypair (bogus_public, bogus_secret);

    client = zmq_socket (ctx, ZMQ_DEALER);
    assert (client);
    rc = zmq_setsockopt (client, ZMQ_CURVE_SERVERKEY, server_public, 41);
    assert (rc == 0);
    rc = zmq_setsockopt (client, ZMQ_CURVE_PUBLICKEY, bogus_public, 41);
    assert (rc == 0);
    rc = zmq_setsockopt (client, ZMQ_CURVE_SECRETKEY, bogus_secret, 41);
    assert (rc == 0);
    rc = zmq_connect (client, my_endpoint);
    assert (rc == 0);
    expect_bounce_fail (server, client);
    close_zero_linger (client);

#ifdef ZMQ_BUILD_DRAFT_API
    event = get_monitor_event(server_mon, NULL, NULL, 0);
    assert (event == ZMQ_EVENT_HANDSHAKE_FAILED_NO_DETAIL); // ZAP handle the error,  not curve_server

    // This event has to be the last one
    event = get_monitor_event(server_mon, NULL, NULL, ZMQ_DONTWAIT);
    assert (event == -1);
#endif

    //  Check CURVE security with NULL client credentials
    //  This must be caught by the curve_server class, not passed to ZAP
    client = zmq_socket (ctx, ZMQ_DEALER);
    assert (client);
    rc = zmq_connect (client, my_endpoint);
    assert (rc == 0);
    expect_bounce_fail (server, client);
    close_zero_linger (client);

#ifdef ZMQ_BUILD_DRAFT_API
    event = get_monitor_event(server_mon, NULL, NULL, 0);

    assert (event == ZMQ_EVENT_HANDSHAKE_FAILED_PROTOCOL);
#endif

    //  Check CURVE security with PLAIN client credentials
    //  This must be caught by the curve_server class, not passed to ZAP
    client = zmq_socket (ctx, ZMQ_DEALER);
    assert (client);
    rc = zmq_setsockopt (client, ZMQ_PLAIN_USERNAME, "admin", 5);
    assert (rc == 0);
    rc = zmq_setsockopt (client, ZMQ_PLAIN_PASSWORD, "password", 8);
    assert (rc == 0);
    rc = zmq_connect (client, "tcp://localhost:9998");
    assert (rc == 0);
    expect_bounce_fail (server, client);
    close_zero_linger (client);

    // Unauthenticated messages from a vanilla socket shouldn't be received
    struct sockaddr_in ip4addr;
    int s;

    unsigned short int port;
    rc = sscanf(my_endpoint, "tcp://127.0.0.1:%hu", &port);
    assert (rc == 1);

    ip4addr.sin_family = AF_INET;
    ip4addr.sin_port = htons (port);
#if defined (ZMQ_HAVE_WINDOWS) && (_WIN32_WINNT < 0x0600)
    ip4addr.sin_addr.s_addr = inet_addr ("127.0.0.1");
#else
    inet_pton(AF_INET, "127.0.0.1", &ip4addr.sin_addr);
#endif

    s = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
    rc = connect (s, (struct sockaddr*) &ip4addr, sizeof (ip4addr));
    assert (rc > -1);
    // send anonymous ZMTP/1.0 greeting
    send (s, "\x01\x00", 2, 0);
    // send sneaky message that shouldn't be received
    send (s, "\x08\x00sneaky\0", 9, 0);
    int timeout = 250;
    zmq_setsockopt (server, ZMQ_RCVTIMEO, &timeout, sizeof (timeout));
    char *buf = s_recv (server);
    if (buf != NULL) {
        printf ("Received unauthenticated message: %s\n", buf);
        assert (buf == NULL);
    }
    close (s);

    //  Check return codes for invalid buffer sizes
    client = zmq_socket (ctx, ZMQ_DEALER);
    assert (client);
    errno = 0;
    rc = zmq_setsockopt (client, ZMQ_CURVE_SERVERKEY, server_public, 123);
    assert (rc == -1 && errno == EINVAL);
    errno = 0;
    rc = zmq_setsockopt (client, ZMQ_CURVE_PUBLICKEY, client_public, 123);
    assert (rc == -1 && errno == EINVAL);
    errno = 0;
    rc = zmq_setsockopt (client, ZMQ_CURVE_SECRETKEY, client_secret, 123);
    assert (rc == -1 && errno == EINVAL);
    rc = zmq_close (client);
    assert (rc == 0);

    //  Shutdown
#ifdef ZMQ_BUILD_DRAFT_API
    close_zero_linger (server_mon);
#endif
    rc = zmq_close (server);
    assert (rc == 0);
    rc = zmq_ctx_term (ctx);
    assert (rc == 0);

    //  Wait until ZAP handler terminates
    zmq_threadclose (zap_thread);

    return 0;
}
