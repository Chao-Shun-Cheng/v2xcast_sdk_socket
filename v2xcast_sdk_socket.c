#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "dsrc_caster_service.h"
#include "error_code_user.h"
#include "poti_caster_service.h"

#ifndef IPC_H
#define IPC_H
#define CLIENT_SOCK_FILE "/tmp/client.sock"
#define SERVER_SOCK_FILE "/tmp/server.sock"
#endif

#define RX_MAX 1460U
#define TX_MAX 1460U

/* Thread type is using for application send and receive thread, the application thread type is an optional method depend on execute platform */
typedef enum app_thread_type { APP_THREAD_TX = 0, APP_THREAD_RX = 1 } app_thread_type_t;

bool app_running = true;
char app_sigaltstack[SIGSTKSZ];
int sock = -1;

int getConnect(int *sock, app_thread_type_t type);
static void *receiver_handler(void *args);
static void *sender_handler(void *args);
static int32_t app_set_thread_name_and_priority(pthread_t thread, app_thread_type_t type, char *p_name, int32_t priority);
void app_signal_handler(int sig_num);
int app_setup_signals();

int main(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    caster_handler_t handler1;
    caster_comm_config_t config1;
    pthread_t thread1;
    pthread_t thread2;
    int ret;
    dsrc_app_thread_config_t app_thread_config;

    setbuf(stdout, NULL);

    if (argc != 4) {
        printf("v2xcast_sdk_socket <IP_of_V2Xcast_service> <is_send> <is_sec 0:none 1:signed 2:unsecured>\n");
        return -1;
    }

    ret = app_setup_signals();
    if (!IS_SUCCESS(ret)) {
        printf("Fail to app_setup_signals\n");
        return -1;
    }

    if (atoi(argv[2]) == 0) { /* receiving thread */
        config1.ip = argv[1];
        config1.caster_id = 0;
        config1.caster_comm_mode = CASTER_MODE_RX;

        ret = dsrc_caster_create(&handler1, &config1);
        if (!IS_SUCCESS(ret)) {
            printf("Failed to create caster, ret:%d!\n", ret);
            return -1;
        }

        if (pthread_create(&thread1, NULL, receiver_handler, (void *) &handler1)) {
            perror("could not create thread for receiver_handler");
            return -1;
        }
        /* If the example is run in Unex device, please using the below functions to set tx and rx message threads name and priority */
        /* If the example is run on other platforms, it is optional to set tx and rx message threads name and priority */
        dsrc_get_app_thread_config(&app_thread_config);  // TODO : understand this function by Kenny
        ret = app_set_thread_name_and_priority(thread1, APP_THREAD_RX, app_thread_config.rx_thread_name, app_thread_config.rx_thread_priority_low);

        pause();
        pthread_kill(thread1, SIGTERM);
        pthread_join(thread1, NULL);
        printf("release caster...\n");
        dsrc_caster_release(handler1);
    } else { /* sending thread */
        config1.ip = argv[1];
        config1.caster_id = 0;
        config1.caster_comm_mode = CASTER_MODE_TX;

        ret = dsrc_caster_create(&handler1, &config1);
        if (!IS_SUCCESS(ret)) {
            printf("Failed to create caster 2, ret:%d!\n", ret);
            return -1;
        }

        if (pthread_create(&thread2, NULL, sender_handler, (void *) &handler1)) {
            perror("could not create thread for sender_handler");
            return -1;
        }

        /* If the example is run in Unex device, please using the below functions to set tx and rx message threads name and priority */
        /* If the example is run on other platforms, it is optional to set tx and rx message threads name and priority */
        dsrc_get_app_thread_config(&app_thread_config);
        ret = app_set_thread_name_and_priority(thread2, APP_THREAD_TX, app_thread_config.tx_thread_name, app_thread_config.tx_thread_priority_low);

        pause();
        pthread_kill(thread2, SIGTERM);
        pthread_join(thread2, NULL);
        printf("release caster...\n");
        dsrc_caster_release(handler1);
    }
    return 0;
}

static int32_t app_set_thread_name_and_priority(pthread_t thread, app_thread_type_t type, char *p_name, int32_t priority)
{
    int32_t result;
    dsrc_app_thread_config_t limited_thread_config;

#ifdef __SET_PRIORITY__
    struct sched_param param;
#endif  // __SET_PRIORITY__
    if (p_name == NULL) {
        return -1;
    }

    /* Check thread priority is in the limited range */
    dsrc_get_app_thread_config(&limited_thread_config);

    if (APP_THREAD_TX == type) {
        /* Check the limited range for tx thread priority */
        if ((priority < limited_thread_config.tx_thread_priority_low) || (priority > limited_thread_config.tx_thread_priority_high)) {
            /* Thread priority is out of range */
            printf("The tx thread priority is out of range (%d-%d): %d \n", limited_thread_config.tx_thread_priority_low,
                   limited_thread_config.tx_thread_priority_high, priority);
            return -1;
        }
    } else if (APP_THREAD_RX == type) {
        /* Check the limited range for rx thread priority */
        if ((priority < limited_thread_config.rx_thread_priority_low) || (priority > limited_thread_config.rx_thread_priority_high)) {
            /* Thread priority is out of range */
            printf("The rx thread priority is out of range (%d-%d): %d \n", limited_thread_config.rx_thread_priority_low,
                   limited_thread_config.rx_thread_priority_high, priority);
            return -1;
        }
    } else {
        /* Target thread type is unknown */
        printf("The thread type is unknown: %d \n", type);
        return -1;
    }

    result = pthread_setname_np(thread, p_name);
    if (result != 0) {
        printf("Can't set thread name: %d (%s)\n", result, strerror(result));
        return -1;
    }

#ifdef __SET_PRIORITY__
    param.sched_priority = priority;
    result = pthread_setschedparam(thread, SCHED_FIFO, &param);
    if (result != 0) {
        printf("Can't set thread priority: %d (%s)\n", result, strerror(result));
        return -1;
    }
#endif  // __SET_PRIORITY__
    return 0;
}

// ----------- maintain signal section ----------- //
void app_signal_handler(int sig_num)
{
    if (sig_num == SIGINT) {
        printf("SIGINT signal!\n");
        close(sock);
        unlink(SERVER_SOCK_FILE);
        unlink(CLIENT_SOCK_FILE);
    }
    if (sig_num == SIGTERM) {
        printf("SIGTERM signal!\n");
        close(sock);
        unlink(SERVER_SOCK_FILE);
        unlink(CLIENT_SOCK_FILE);
    }
    app_running = false;
}

int app_setup_signals()
{
    stack_t sigstack;
    struct sigaction sa;
    int ret = -1;

    sigstack.ss_sp = app_sigaltstack;
    sigstack.ss_size = SIGSTKSZ;
    sigstack.ss_flags = 0;
    if (sigaltstack(&sigstack, NULL) == -1) {
        perror("signalstack()");
        goto END;
    }

    sa.sa_handler = app_signal_handler;
    sa.sa_flags = SA_ONSTACK;
    if (sigaction(SIGINT, &sa, NULL) != 0) {
        perror("sigaction");
        goto END;
    }
    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        perror("sigaction");
        goto END;
    }

    ret = 0;
END:
    return ret;
}
// ----------- maintain signal section ----------- //

// ----------- dsrc receive section ----------- //
static void *receiver_handler(void *args)
{
    caster_handler_t *caster_handler = (caster_handler_t *) args;
    dsrc_rx_info_t rx_info;
    uint8_t rx_buf[RX_MAX];
    size_t len;
    int ret;
    if (getConnect(&sock, APP_THREAD_RX) > 0) {
        printf("get connect.\n");
    } else {
        pthread_exit(NULL);
    }

    while (app_running) {
        printf("-----------------------\n");
        ret = dsrc_caster_rx(*caster_handler, &rx_info, rx_buf, &len);
        if (IS_SUCCESS(ret)) {
            printf("Received %zu bytes!\n", len);
            if (send(sock, (char *) rx_buf, (int) len, 0) < 0) {
                perror("send() failed");
                break;
            } else {
                printf("Send message to ROS\n");
            }
        } else {
            /* The user may still get the payload with len is non-zero. */
            /* It means the data has some problems, such as security check failure */
            /* Users can determine to trust the data or not by themself */
            if (len != 0) {
                printf("Received %zu bytes, but had some issues! err code is:%d, msg = %s\n", len, ret, ERROR_MSG(ret));
            } else {
                printf("Failed to receive data, err code is:%d, msg = %s\n", ret, ERROR_MSG(ret));
            }
        }
        fflush(stdout);
    }
    pthread_exit(NULL);
}
// ----------- dsrc receive section ----------- //

static void *sender_handler(void *args)
{
    caster_handler_t *caster_handler = (caster_handler_t *) args;
    char *tx_buf = NULL;
    int tx_buf_len = 112;
    int ret;
    if (getConnect(&sock, APP_THREAD_TX) > 0) {
        printf("get connect.\n");
    } else {
        pthread_exit(NULL);
    }

    while (app_running) {
        printf("-----------------------\n");
        char data[tx_buf_len];
        if (recv(sock, data, tx_buf_len, 0) < 0) {
            perror("recv() failed");
            break;
        } else {
            printf("Receive message from ROS\n");
        }
        tx_buf = &data;
        ret = dsrc_caster_tx(*caster_handler, NULL, (uint8_t *) tx_buf, (size_t) tx_buf_len);

        if (IS_SUCCESS(ret)) {
            printf("Transmitted %zu bytes!\n", tx_buf_len);
        } else {
            printf("Failed to transmit data, err code is:%d, msg = %s\n", ret, ERROR_MSG(ret));
        }
        fflush(stdout);
        sleep(1);
    }
    pthread_exit(NULL);
}

int getConnect(int *sock, app_thread_type_t type)
{
    struct sockaddr_un addr;
    socklen_t len;

    if ((*sock = socket(PF_UNIX, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        return -1;
    } else {
        printf("socket suceess\n");
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SERVER_SOCK_FILE);
    
    if(APP_THREAD_RX == type) {
        if (connect(*sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
			perror("connect");
			return -1;
		}
    }
    else if(APP_THREAD_TX == type) {
        // unlink(SERVER_SOCK_FILE);
        if (bind(*sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
            perror("bind");
            return -1;
        }
    }
    
    return 1;
}