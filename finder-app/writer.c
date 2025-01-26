#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>

int main(int argc, char *argv[]) {
    // Open syslog with LOG_USER facility
    openlog("writer", LOG_PID, LOG_USER);

    // Check if the correct number of arguments are provided
    if (argc != 3) {
        syslog(LOG_ERR, "Invalid number of arguments: %d (expected 2)", argc - 1);
        fprintf(stderr, "Usage: %s <file> <string>\n", argv[0]);
        closelog();
        return 1;
    }

    // Extract arguments
    const char *file_path = argv[1];
    const char *text_to_write = argv[2];

    // Log the writing operation with LOG_DEBUG level
    syslog(LOG_DEBUG, "Writing %s to %s", text_to_write, file_path);

    // Open the file for writing
    FILE *file = fopen(file_path, "w");
    if (file == NULL) {
        syslog(LOG_ERR, "Failed to open file: %s", file_path);
        perror("Error");
        closelog();
        return 1;
    }

    // Write the text to the file
    if (fprintf(file, "%s\n", text_to_write) < 0) {
        syslog(LOG_ERR, "Failed to write to file: %s", file_path);
        perror("Error");
        fclose(file);
        closelog();
        return 1;
    }

    // Close the file
    fclose(file);

    // Close syslog
    closelog();

    return 0;
}