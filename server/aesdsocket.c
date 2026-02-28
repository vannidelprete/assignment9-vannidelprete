#include <asm-generic/errno-base.h>
#include <asm-generic/socket.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syslog.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include "../aesd-char-driver/aesd_ioctl.h"

#define PORT 9000
#define USE_AESD_CHAR_DEVICE 1
#if USE_AESD_CHAR_DEVICE
    #define DATA_FILE "/dev/aesdchar"
#else
    #define DATA_FILE "/var/tmp/aesdsocket"
#endif
#define BUFFER_SIZE 1024

// Global variables for signal handling
static volatile bool signal_received = false;
static int server_fd = -1;
static int data_fd = -1;


void signal_handler(int signo)
{
    if (signo == SIGINT || signo == SIGTERM)
    {
        syslog(LOG_INFO, "Caught signal, exiting");
        signal_received = true;
    }
}

int setup_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;

    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
        syslog(LOG_ERR, "Failed to setup SIGINT handler: %s", strerror(errno));
        return -1;
    }

    if (sigaction(SIGTERM, &sa, NULL) == -1)
    {
        syslog(LOG_ERR, "Failed to setup SIGTERM handler: %s", strerror(errno));
        return -1;
    }

    return 0;
}

void cleanup(void)
{
    if (server_fd != -1)
    {
        close(server_fd);
        server_fd = -1;
    }

    if (data_fd != -1)
    {
        close(data_fd);
        data_fd = -1;
    }

#if !USE_AESD_CHAR_DEVICE
    if (unlink(DATA_FILE) == -1 && errno != ENOENT)
    {
        syslog(LOG_ERR, "Failed to delete data file: %s", strerror(errno));
    }
#endif

    closelog();
}

int daemonize(void)
{
    pid_t pid = fork();

    if (pid < 0)
    {
        syslog(LOG_ERR, "Fork failed: %s", strerror(errno));
        return -1;
    }

    if (pid > 0)
    {
        exit(EXIT_SUCCESS);
    }

    if (setsid() < 0)
    {
        syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
        return -1;
    }

    if (chdir("/") < 0)
    {
        syslog(LOG_ERR, "chdir failed: %s", strerror(errno));
        return -1;
    }

    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Redirect standard file descriptors to /dev/null
    int dev_null = open("/dev/null", O_RDWR);
    if (dev_null == -1)
    {
        syslog(LOG_ERR, "Failed to open /dev/null: %s", strerror(errno));
        return -1;
    }

    dup2(dev_null, STDIN_FILENO);
    dup2(dev_null, STDOUT_FILENO);
    dup2(dev_null, STDERR_FILENO);

    if (dev_null > STDERR_FILENO)
    {
        close(dev_null);
    }

    return 0;
}

int handle_connection(int client_fd)
{
    char *recv_buf = NULL;
    size_t recv_buf_size = 0;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    /* Accumulate the full packet until newline */
    while (!signal_received)
    {
        bytes_received = recv(client_fd, buffer, BUFFER_SIZE, 0);

        if (bytes_received < 0)
        {
            syslog(LOG_ERR, "recv failed: %s", strerror(errno));
            free(recv_buf);
            return -1;
        }

        if (bytes_received == 0)
            break;

        char *tmp = realloc(recv_buf, recv_buf_size + bytes_received);
        if (!tmp)
        {
            syslog(LOG_ERR, "realloc failed");
            free(recv_buf);
            return -1;
        }
        recv_buf = tmp;
        memcpy(recv_buf + recv_buf_size, buffer, bytes_received);
        recv_buf_size += bytes_received;

        if (memchr(recv_buf, '\n', recv_buf_size))
            break;
    }

    if (recv_buf_size == 0)
    {
        free(recv_buf);
        return 0;
    }

    /* Check if the received string is an AESDCHAR_IOCSEEKTO:X,Y command */
    uint32_t write_cmd, write_cmd_offset;
    if (sscanf(recv_buf, "AESDCHAR_IOCSEEKTO:%u,%u", &write_cmd, &write_cmd_offset) == 2)
    {
        /* Open the device with O_RDWR and keep the same fd for the read after ioctl */
        data_fd = open(DATA_FILE, O_RDWR);
        if (data_fd == -1)
        {
            syslog(LOG_ERR, "Failed to open data file: %s", strerror(errno));
            free(recv_buf);
            return -1;
        }

        struct aesd_seekto seekto;
        seekto.write_cmd = write_cmd;
        seekto.write_cmd_offset = write_cmd_offset;

        if (ioctl(data_fd, AESDCHAR_IOCSEEKTO, &seekto) == -1)
        {
            syslog(LOG_ERR, "ioctl failed: %s", strerror(errno));
            close(data_fd);
            data_fd = -1;
            free(recv_buf);
            return -1;
        }

        /* Read from the position set by ioctl and send back to client */
        while (!signal_received)
        {
            ssize_t bytes_read = read(data_fd, buffer, BUFFER_SIZE);

            if (bytes_read < 0)
            {
                syslog(LOG_ERR, "read failed: %s", strerror(errno));
                close(data_fd);
                data_fd = -1;
                free(recv_buf);
                return -1;
            }

            if (bytes_read == 0)
                break;

            ssize_t bytes_sent = send(client_fd, buffer, bytes_read, 0);
            if (bytes_sent != bytes_read)
            {
                syslog(LOG_ERR, "send failed: %s", strerror(errno));
                close(data_fd);
                data_fd = -1;
                free(recv_buf);
                return -1;
            }
        }

        close(data_fd);
        data_fd = -1;
    }
    else
    {
        /* Normal behavior: write to device */
        data_fd = open(DATA_FILE, O_CREAT | O_RDWR | O_APPEND, 0644);
        if (data_fd == -1)
        {
            syslog(LOG_ERR, "Failed to open data file: %s", strerror(errno));
            free(recv_buf);
            return -1;
        }

        ssize_t bytes_written = write(data_fd, recv_buf, recv_buf_size);
        if (bytes_written != (ssize_t)recv_buf_size)
        {
            syslog(LOG_ERR, "Failed to write to data file: %s", strerror(errno));
            close(data_fd);
            data_fd = -1;
            free(recv_buf);
            return -1;
        }

        close(data_fd);

        /* Reopen and read everything from the beginning, send to client */
        data_fd = open(DATA_FILE, O_RDONLY);
        if (data_fd == -1)
        {
            syslog(LOG_ERR, "Failed to reopen data file: %s", strerror(errno));
            free(recv_buf);
            return -1;
        }

        while (!signal_received)
        {
            ssize_t bytes_read = read(data_fd, buffer, BUFFER_SIZE);

            if (bytes_read < 0)
            {
                syslog(LOG_ERR, "read failed: %s", strerror(errno));
                close(data_fd);
                data_fd = -1;
                free(recv_buf);
                return -1;
            }

            if (bytes_read == 0)
                break;

            ssize_t bytes_sent = send(client_fd, buffer, bytes_read, 0);
            if (bytes_sent != bytes_read)
            {
                syslog(LOG_ERR, "send failed: %s", strerror(errno));
                close(data_fd);
                data_fd = -1;
                free(recv_buf);
                return -1;
            }
        }

        close(data_fd);
        data_fd = -1;
    }

    free(recv_buf);
    return 0;
}

int main(int argc, char* argv[])
{
    int client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int opt = 1;
    bool daemon_mode = false;

    // Opening syslog
    openlog("aesdsocket", LOG_PID, LOG_USER);

    // Parse command line arguments
    if (argc > 1 && strcmp(argv[1], "-d") == 0)
    {
        daemon_mode = true;
    }

    // Setup signal handlers
    if (setup_signal_handlers() == -1)
    {
        cleanup();
        return -1;
    }

    // Create socket (I use AF_INET. PF_INET is deprecated)
    server_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
    {
        syslog(LOG_ERR, "Socket creation failed: %s", strerror(errno));
        cleanup();
        return -1;
    }

    // Set socket options to reuse address
    // This allows the server to bind to the port immediately after restart,
    // even if the port is still in TIME_WAIT state from a previous connection.
    // Without this, you would get "Address already in use" error and have to wait.
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        syslog(LOG_ERR, "setsockopt failed: %s", strerror(errno));
        cleanup();
        return -1;
    }

    // Bind socket
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1)
    {
        syslog(LOG_ERR, "Bind failed: %s", strerror(errno));
        cleanup();
        return -1;
    }

    // Daemonize if requested
    if (daemon_mode)
    {
        if (daemonize() == -1)
        {
            cleanup();
            return -1;
        }
    }

    // Listen for connections
    if (listen(server_fd, 10) == -1)
    {
        syslog(LOG_ERR, "Listen failed: %s", strerror(errno));
        cleanup();
        return -1;
    }

    syslog(LOG_INFO, "Server listening on port %d", PORT);

    // Main server loop
    while (!signal_received)
    {
        // Accept connection
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_len);

        if (client_fd == -1)
        {
            if (errno == EINTR)
            {
                // Interrupted by signal
                continue;
            }
            syslog(LOG_ERR, "Accept failed: %s", strerror(errno));
            continue;
        }

        // Log accepted connection
        char* client_ip = inet_ntoa(client_addr.sin_addr);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        // Handle connection
        handle_connection(client_fd);

        syslog(LOG_INFO, "Closed connection from %s", client_ip);

        close(client_fd);
    }

    cleanup();
    return 0;
}
