/*
# Copyright 2025 University of Kentucky
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
*/

/* 
Please specify the group members here

# Student #1: Daniel Howard
# Student #2: -
# Student #3: -

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pthread.h>

#define MAX_EVENTS 64
#define MESSAGE_SIZE 16
#define DEFAULT_CLIENT_THREADS 4

char *server_ip = "127.0.0.1";
int server_port = 12345;
int num_client_threads = DEFAULT_CLIENT_THREADS;
int num_requests = 1000000;

/*
 * This structure is used to store per-thread data in the client
 */
typedef struct {
    int epoll_fd;        /* File descriptor for the epoll instance, used for monitoring events on the socket. */
    int socket_fd;       /* File descriptor for the client socket connected to the server. */
    long long total_rtt; /* Accumulated Round-Trip Time (RTT) for all messages sent and received (in microseconds). */
    long total_messages; /* Total number of messages sent and received. */
    float request_rate;  /* Computed request rate (requests per second) based on RTT and total messages. */
} client_thread_data_t;

/*
 * This function runs in a separate client thread to handle communication with the server
 */
void *client_thread_func(void *arg) {
    client_thread_data_t *data = (client_thread_data_t *)arg;
    struct epoll_event event, events[MAX_EVENTS];
    char send_buf[MESSAGE_SIZE] = "ABCDEFGHIJKMLNOP"; /* Send 16-Bytes message every time */
    char recv_buf[MESSAGE_SIZE];
    struct timeval start, end;
    int tx_cnt = 0, rx_cnt = 0, lost_pkt_cnt;

    // Hint 1: register the "connected" client_thread's socket in the its epoll instance
    // Hint 2: use gettimeofday() and "struct timeval start, end" to record timestamp, which can be used to calculated RTT.
    event.events = EPOLLIN;
    epoll_ctl(data->epoll_fd, EPOLL_CTL_ADD, data->socket_fd, &event);

    /*
     * It sends messages to the server, waits for a response using epoll,
     * and measures the round-trip time (RTT) of this request-response.
     */
    for (int i = 0; i < num_requests; i++){
        gettimeofday(&start, NULL);
        if (send(data->socket_fd, send_buf, MESSAGE_SIZE, 0) > -1){
            tx_cnt += 1;
        }
        if (epoll_wait(data->epoll_fd, events, MAX_EVENTS, 1000) <= 0){
            continue;
        }
        if (recv(data->socket_fd, recv_buf, MESSAGE_SIZE, 0) > -1){
            rx_cnt += 1;
        }
        gettimeofday(&end, NULL);
        long long rtt = (end.tv_usec - start.tv_usec) + ((end.tv_sec - start.tv_sec) * 1000000ll);
        data->total_rtt += rtt;
        data->total_messages += 1;
    }
    lost_pkt_cnt = tx_cnt - rx_cnt;
    if (lost_pkt_cnt > 0){
        printf("Thread lost %d packet(s)\n", lost_pkt_cnt);
    }

    /*
     * The function exits after sending and receiving a predefined number of messages (num_requests). 
     * It calculates the request rate based on total messages and RTT
     */
    data->request_rate = (float)data->total_messages / ((float)data->total_rtt / 1000000.0);
    close(data->socket_fd);
    close(data->epoll_fd);
    return NULL;
}

/*
 * This function orchestrates multiple client threads to send requests to a server,
 * collect performance data of each threads, and compute aggregated metrics of all threads.
 */
void run_client() {
    pthread_t threads[num_client_threads];
    client_thread_data_t thread_data[num_client_threads];
    struct sockaddr_in server_addr;

    /*
     * Create sockets and epoll instances for client threads
     * and connect these sockets of client threads to the server
     */
    int c, t;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &(server_addr.sin_addr));

    for (int i = 0; i < num_client_threads; i++){
        thread_data[i].total_rtt = 0;
        thread_data[i].total_messages = 0;
        thread_data[i].request_rate = 0;
        thread_data[i].socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (thread_data[i].socket_fd < 0) {printf("Client socket failed\n"); exit (-1);}
        thread_data[i].epoll_fd = epoll_create1(0);
        if (thread_data[i].epoll_fd < 0) {printf("Client epoll failed\n"); exit (-1);}
        c = connect(thread_data[i].socket_fd, (struct sockaddr *) &server_addr, sizeof(server_addr));
        if (c < 0) {printf("Client connection failed\n"); exit (-1);}
    }

    // Hint: use thread_data to save the created socket and epoll instance for each thread
    // You will pass the thread_data to pthread_create() as below
    for (int i = 0; i < num_client_threads; i++){
        t = pthread_create(&threads[i], NULL, client_thread_func, &thread_data[i]);
        if (t != 0) {printf("Client thread creation failed\n"); exit(-1);}
    }

    /*
     * Wait for client threads to complete and aggregate metrics of all client threads
     */
    long long total_rtt = 0;
    long total_messages = 0;
    float total_request_rate = 0;

    for (int i = 0; i < num_client_threads; i++){
        pthread_join(threads[i], NULL);
        total_rtt += thread_data[i].total_rtt;
        total_messages += thread_data[i].total_messages;
        total_request_rate += thread_data[i].request_rate;
    }
    printf("Total messages recieved: %ld/%d\n", total_messages, num_client_threads * num_requests);
    printf("Average RTT: %lld us\n", total_rtt / total_messages);
    printf("Total Request Rate: %f messages/s\n", total_request_rate);
}

void run_server() {
    int s, b, e;
    struct sockaddr_in server_addr, client_addr;
    struct epoll_event event, events[MAX_EVENTS];
    socklen_t client_addr_len = sizeof(client_addr);

    /* 
     * Server creates listening socket and epoll instance.
     * Server registers the listening socket to epoll
     */
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &(server_addr.sin_addr));
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {printf("Server socket failed\n"); exit(-1);}
    b = bind(s, (struct sockaddr *) &server_addr, sizeof(server_addr));
    if (b < 0) {printf("Server bind failed\n"); exit(-1);}
    e = epoll_create1(0);
    if (e < 0){printf("Server epoll failed\n");exit (-1);}
    event.events = EPOLLIN;
    event.data.fd = s;
    epoll_ctl(e, EPOLL_CTL_ADD, s, &event);

    /* Server's run-to-completion event loop */
    int bytes;
    while (1) {
        /*
         * Server uses epoll to handle connection establishment with clients
         * or receive the message from clients and echo the message back
         */
        epoll_wait(e, events, MAX_EVENTS, -1);
        char buf[MESSAGE_SIZE];
        bytes = recvfrom(events[0].data.fd, buf, MESSAGE_SIZE, 0, (struct sockaddr *)&client_addr, &client_addr_len);
        if (bytes > 0){
            sendto(events[0].data.fd, buf, bytes, 0, (struct sockaddr *)&client_addr, client_addr_len);
        }else{
            close(events[0].data.fd);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "server") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);

        run_server();
    } else if (argc > 1 && strcmp(argv[1], "client") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);
        if (argc > 4) num_client_threads = atoi(argv[4]);
        if (argc > 5) num_requests = atoi(argv[5]);

        run_client();
    } else {
        printf("Usage: %s <server|client> [server_ip server_port num_client_threads num_requests]\n", argv[0]);
    }

    return 0;
}