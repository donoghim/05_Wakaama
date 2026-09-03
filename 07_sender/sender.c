#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_SOCKET_PATH "/tmp/lwm2mserver-control.sock"
#define REQUEST_BUFFER_SIZE 1024

static void print_usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s -t SECONDS -c CLIENT_ID -r URI -d DATA [OPTION]\n"
            "  -t SECONDS     Write interval in seconds\n"
            "  -c CLIENT_ID   LwM2M server client ID (0-65535)\n"
            "  -r URI         Resource URI, for example /10250/0/1\n"
            "  -d DATA        Value to write; use 'now' for Unix epoch seconds\n"
            "  -s PATH        Server control socket (default: %s)\n"
            "  -n COUNT       Number of writes; default is unlimited\n"
            "  -i             Write immediately, then repeat at the interval\n"
            "  -v             Print each queued request\n",
            program, DEFAULT_SOCKET_PATH);
}

static int parse_unsigned(const char *text, unsigned long maximum, unsigned long *value)
{
    char *end;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || *text == '\0' || *end != '\0' || parsed > maximum)
    {
        return -1;
    }

    *value = parsed;
    return 0;
}

static int queue_write(const char *socket_path, uint16_t client_id, const char *uri, const char *data)
{
    struct sockaddr_un address;
    char request[REQUEST_BUFFER_SIZE];
    int socket_fd;
    int length;

    length = snprintf(request, sizeof(request), "WRITE\t%" PRIu16 "\t%s\t%s", client_id, uri, data);
    if (length < 0 || (size_t)length >= sizeof(request))
    {
        fprintf(stderr, "Request is too long.\n");
        return -1;
    }

    if (strlen(socket_path) >= sizeof(address.sun_path))
    {
        fprintf(stderr, "Socket path is too long.\n");
        return -1;
    }

    socket_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (socket_fd < 0)
    {
        perror("socket");
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, socket_path, strlen(socket_path) + 1);

    if (sendto(socket_fd, request, (size_t)length, 0, (struct sockaddr *)&address, sizeof(address)) != length)
    {
        perror("sendto");
        close(socket_fd);
        return -1;
    }

    close(socket_fd);
    return 0;
}

int main(int argc, char *argv[])
{
    const char *data = NULL;
    const char *socket_path = DEFAULT_SOCKET_PATH;
    const char *uri = NULL;
    unsigned long client_value = 0;
    unsigned long count = 0;
    unsigned long interval = 0;
    uint16_t client_id;
    int immediate = 0;
    int option;
    int verbose = 0;
    struct timespec next_run;

    while ((option = getopt(argc, argv, "t:c:r:d:s:n:ivh")) != -1)
    {
        switch (option)
        {
        case 't':
            if (parse_unsigned(optarg, ULONG_MAX, &interval) != 0 || interval == 0)
            {
                fprintf(stderr, "Invalid interval: %s\n", optarg);
                return 2;
            }
            break;
        case 'c':
            if (parse_unsigned(optarg, UINT16_MAX, &client_value) != 0)
            {
                fprintf(stderr, "Invalid client ID: %s\n", optarg);
                return 2;
            }
            break;
        case 'r':
            uri = optarg;
            break;
        case 'd':
            data = optarg;
            break;
        case 's':
            socket_path = optarg;
            break;
        case 'n':
            if (parse_unsigned(optarg, ULONG_MAX, &count) != 0 || count == 0)
            {
                fprintf(stderr, "Invalid write count: %s\n", optarg);
                return 2;
            }
            break;
        case 'i':
            immediate = 1;
            break;
        case 'v':
            verbose = 1;
            break;
        default:
            print_usage(argv[0]);
            return option == 'h' ? 0 : 2;
        }
    }

    if (optind != argc || interval == 0 || uri == NULL || data == NULL || uri[0] != '/')
    {
        print_usage(argv[0]);
        return 2;
    }

    client_id = (uint16_t)client_value;
    if (clock_gettime(CLOCK_MONOTONIC, &next_run) != 0)
    {
        perror("clock_gettime");
        return 1;
    }
    if (!immediate)
    {
        next_run.tv_sec += (time_t)interval;
    }

    for (unsigned long sent = 0; count == 0 || sent < count; ++sent)
    {
        char current_time[32];
        const char *value = data;

        if (strcmp(data, "now") == 0)
        {
            time_t epoch = time(NULL);
            if (epoch == (time_t)-1)
            {
                perror("time");
                return 1;
            }
            snprintf(current_time, sizeof(current_time), "%jd", (intmax_t)epoch);
            value = current_time;
        }

        if (queue_write(socket_path, client_id, uri, value) != 0)
        {
            return 1;
        }
        if (verbose)
        {
            printf("queued: client=%" PRIu16 " uri=%s data=%s\n", client_id, uri, value);
        }

        if (count != 0 && sent + 1 == count)
        {
            break;
        }
        next_run.tv_sec += (time_t)interval;
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_run, NULL) == EINTR)
        {
        }
    }

    return 0;
}