#include <stdio.h>
#include <stdlib.h>
#include <sys/syslog.h>
#include <syslog.h>
#include <string.h>

int main(int argc, char *argv[])
{
    // open connection to syslog
    openlog("writer", LOG_PID | LOG_CONS, LOG_USER);

    if (argc != 3)
    {
        syslog(LOG_ERR, "Invalid number of arguments: %d (expected 2)", argc - 1);
        fprintf(stderr, "Usage: %s <file> <string>\n", argv[0]);
        closelog();
        return 1;
    }

    const char* writefile = argv[1];
    const char* writestr = argv[2];

    // debug logging message
    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

    FILE *fp = fopen(writefile, "w");
    if (fp == NULL)
    {
        syslog(LOG_ERR, "Failed to open file %s for writing", writefile);
        perror("fopen");
        closelog();
        return 1;
    }

    // write string into file
    if (fprintf(fp, "%s\n", writestr) < 0)
    {
        syslog(LOG_ERR, "Faile to write to file %s", writefile);
        perror("fprintf");
        fclose(fp);
        closelog();
        return 1;
    }

    // close file
    if (fclose(fp) != 0)
    {
        syslog(LOG_ERR, "Failed to close file %s", writefile);
        perror("fclose");
        closelog();
        return 1;
    }

    //close connection to syslog
    closelog();

    return 0;
}
