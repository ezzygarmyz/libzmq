/*
    Copyright (c) 2007-2020 Contributors as noted in the AUTHORS file

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
#include "testutil_unity.hpp"

SETUP_TEARDOWN_TESTCONTEXT

void test ()
{
    //  Create a publisher
    void *publisher = test_context_socket (ZMQ_PUB);
    char my_endpoint[MAX_SOCKET_STRING];

    //  Bind publisher
    test_bind (publisher, "inproc://soname", my_endpoint, MAX_SOCKET_STRING);

    //  Create a subscriber
    void *subscriber = test_context_socket (ZMQ_SUB);
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (subscriber, my_endpoint));

    //  Subscribe to all messages.
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (subscriber, ZMQ_SUBSCRIBE, "topicprefix1", 0));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (subscriber, ZMQ_SUBSCRIBE, "aa", 0));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (subscriber, ZMQ_SUBSCRIBE, "bb", 0));

    //  Wait a bit till the subscription gets to the publisher
    msleep (SETTLE_TIME);
    msleep (SETTLE_TIME);
    msleep (SETTLE_TIME);

    //  Send an empty message
    send_string_expect_success (publisher, "topicprefix1: hello world", 0);

    //  Receive the message in the subscriber
    recv_string_expect_success (subscriber, "topicprefix1: hello world", 0);

    int num_subs = 0;
    size_t num_subs_len = sizeof (num_subs);
    TEST_ASSERT_SUCCESS_ERRNO (zmq_getsockopt (
      publisher, ZMQ_SUBSCRIPTION_COUNT, &num_subs, &num_subs_len));
    TEST_ASSERT_EQUAL_INT (
      1, num_subs); // FIXME: why do I get 1... I would expect 3 !!!

    //  Clean up.
    test_context_socket_close (publisher);
    test_context_socket_close (subscriber);
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test);
    return UNITY_END ();
}
