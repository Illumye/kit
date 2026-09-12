#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <stdlib.h>

#define TS_BUF_LENGTH 30

// Possibilité d'utiliser un enum ??
#define LOG_TIME  0x01
#define LOG_DATE  0x02
#define LOG_USER  0x04
#define LOG_COUNT 0x08
#define LOG_ALL   0xFF

static const char *get_username(void) {
    const char *name = getenv("LOGNAME");
    if (name && *name) {
        return name;
    }

    name = getenv("USER");
    if (name && *name) {
        return name;
    }

    return "unknown";
}

void logmsg(FILE *fp, const char *message, uint8_t options) {
    static uint64_t logcount = 0;

    time_t time_val;
    char timestamp[TS_BUF_LENGTH];
    char datestamp[TS_BUF_LENGTH];
    struct tm *tm_info;

    time_val = time(NULL);
    tm_info = localtime(&time_val);

    strftime(datestamp, TS_BUF_LENGTH, "%F (%a)", tm_info);
    strftime(timestamp, TS_BUF_LENGTH, "%H:%M:%S", tm_info);

    if (options & LOG_COUNT)
        fprintf(fp, "%" PRIu64 ", ", ++logcount);
    if (options & LOG_DATE)
        fprintf(fp, "%s, ", datestamp);
    if (options & LOG_TIME)
        fprintf(fp, "%s, ", timestamp);
    if (options & LOG_USER)
        fprintf(fp, "%s, ", get_username());
    fprintf(fp, "%s\n", message);
}

int main(void) {
    logmsg(stdout, "Things are running fine.", 0);
    logmsg(stdout, "Hmmm... maybe not. What's this ?", LOG_USER | LOG_DATE);
    logmsg(stdout, "The wheels are coming off !", LOG_TIME | LOG_USER | LOG_COUNT);
    logmsg(stdout, "AAAAARGH.", LOG_COUNT);
}