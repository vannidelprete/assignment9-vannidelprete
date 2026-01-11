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

#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
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

    // Delete the data file
    if (unlink(DATA_FILE) == -1 && errno != ENOENT)
    {
        syslog(LOG_ERR, "Failed to delete data file: %s", strerror(errno));
    }

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
    char buffer[BUFFER_SIZE];
    size_t bytes_received;
    bool packet_complete = false;

    // Open data file in append mode
    data_fd = open(DATA_FILE, O_CREAT | O_RDWR | O_APPEND, 0644);
    if (data_fd == -1)
    {
        syslog(LOG_ERR, "Failed to open data file: %s", strerror(errno));
        return -1;
    }

    // Receive data from client
    while (!signal_received)
    {
        bytes_received = recv(client_fd, buffer, BUFFER_SIZE, 0);

        if (bytes_received < 0)
        {
            syslog(LOG_ERR, "recv failed: %s", strerror(errno));
            close(data_fd);
            data_fd = -1;
            return -1;
        }

        if (bytes_received == 0)
        {
            // Connection closed by client
            break;
        }

        ssize_t bytes_written = write(data_fd, buffer, bytes_received);
        if (bytes_written != bytes_received)
        {
            syslog(LOG_ERR, "Failed to write to data file: %s", strerror(errno));
            close(data_fd);
            data_fd = -1;
            return -1;
        }

        // Check if packet is complete (contains newline)
        for (ssize_t i = 0; i < bytes_received; i++)
        {
            if (buffer[i] == '\n')
            {
                packet_complete = true;
                break;
            }
        }

        if (packet_complete)
        {
            break;
        }
    }

    // Send file contents back to client
    lseek(data_fd, 0, SEEK_SET);

    while (!signal_received)
    {
        ssize_t bytes_read = read(data_fd, buffer, BUFFER_SIZE);

        if (bytes_read < 0)
        {
            syslog(LOG_ERR, "Failed to read from data file: %s", strerror(errno));
            close(data_fd);
            data_fd = -1;
            return -1;
        }

        if (bytes_read == 0)
        {
            // End of file
            break;
        }

        ssize_t bytes_sent = send(client_fd, buffer, bytes_read, 0);
        if (bytes_sent != bytes_read)
        {
            syslog(LOG_ERR, "send failed: %s", strerror(errno));
            close(data_fd);
            data_fd = -1;
            return -1;
        }
    }

    close(data_fd);
    data_fd = -1;

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
