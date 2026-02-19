/**
 * filter.c:
 * Filter ANSI sequences to keep only the ones producing output and removing
 * query and mode settings ones. So it can be used on a `record` recording to
 * re display what was recorded.
 *
 * (C) 2026 Laurent Demailly <ldemailly at gmail> and contributors.
 * Licensed under Apache-2.0 (see LICENSE).
 */
#include "ansipixels.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

enum {
#if DEBUG
    // short on purpose for testing to trigger potential bugs with half complete sequences.
    BUF_SIZE = 3
#else
    BUF_SIZE = 1 << 16
#endif
};

static buffer quoted = {0};
static int frames_limit = -1; // default to no frame limit
static int frames_count = 0;


void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [flags] [filename or stdin]\n", prog);
    fprintf(stderr, "Flags:\n");
    fprintf(stderr, "  -h, --help       show this help message\n");
    fprintf(stderr, "  -n, --frames <n> stop after filtering n frames (clear screens)\n");
    fprintf(stderr, "  -a, --all        filters all ANSI sequences, leaving only the text content\n");
    fprintf(stderr, "  -p, --pause      pause at the end\n");
}

typedef enum filter_mode {
    FILTER_DEFAULT, // only filter query and mode settings sequences
    FILTER_ALL      // filter all ANSI sequences, leaving only the text content
} filter_mode;

static bool filterALL(buffer *input, buffer *output);
static bool filterDefault(buffer *input, buffer *output);

// Returns true unless we reached the frames limit.
bool filter(buffer *input, buffer *output, filter_mode mode) {
    char *esc = memchr(input->data, '\x1b', input->size);
    size_t n = input->size;
    if (esc != NULL) {
        n = esc - input->data;
    }
    LOG_DEBUG(
        "Filtering %s (%zu bytes), esc at %zd",
        debug_buf(&quoted, *input),
        input->size,
        esc != NULL ? esc - input->data : -1
    );
    transfer(output, input, n); // part before first escape character, possibly all of it if no escape character found
    if (esc == NULL) {
        // No ANSI sequences, we just copied input to output, nothing left in input.
        LOG_DEBUG("No ANSI sequence found, transferred all %zu bytes to output", n);
        assert(input->size == 0);
        return true;
    }
    LOG_DEBUG("Input post transfer is now %s", debug_buf(&quoted, *input));
    if (input->size < 3) {
        LOG_DEBUG("Not enough data to contain an ANSI sequence, waiting for more data to read");
        // Not enough data to contain a full ANSI sequence, wait for more data to read.
        return true;
    }
    switch (mode) {
    case FILTER_DEFAULT:
        return filterDefault(input, output);
    case FILTER_ALL:
        return filterALL(input, output);
    }
}

static bool filterALL(buffer *input, buffer *output) {
    int c = input->data[1]; // should be ESC, we assert it:
    LOG_DEBUG("Found ANSI sequence starting with ESC %d (%c)", c, c);
    switch (c) {
    case '7':
    case '8': // DECSC/DECRC save/restore cursor position, we just remove it.
        LOG_DEBUG("Found DECSC/DECRC sequence ESC %c, removing it", c);
        consume(input, 2); // remove the ESC and the 7/8 byte, and filter the rest recursively
        return filter(input, output, FILTER_ALL);
    case '[': // CSI sequence.
        LOG_DEBUG("Found CSI sequence: %s", debug_buf(&quoted, *input));
        // We skip the whole ANSI sequence, which ends with a letter (A-Z or a-z) after the ESC[ and parameters.
        // get to closing 0x40 - 0x7E byte
        for (int i = 2; i < (int)input->size; i++) {
            c = input->data[i];
            if (c >= 0x40 && c <= 0x7E) {
                if (c=='J') {
                    frames_count++;
                    LOG_DEBUG("Found clear screen sequence, frames count now %d", frames_count);
                    if (frames_limit > 0 && frames_count >= frames_limit) {
                        LOG_DEBUG("Reached frames limit of %d, stopping processing", frames_limit);
                        return false; // stop processing
                    }
                }
                // found end of ANSI sequence, we skip it and filter the rest recursively
                LOG_DEBUG("Found end of ANSI sequence %c at %d, recursing", c, i);
                consume(input, i + 1);             // remove the whole ANSI sequence from input
                return filter(input, output, FILTER_ALL); // filter the rest of the input
            }
        }
        LOG_DEBUG("Did not find end of ANSI sequence, waiting for more data to read");
        break;
    default:
        LOG_ERROR("\nFound other ANSI sequence starting with ESC %d %c\n", c, c);
#if DEBUG
        debug_print_buf(*input);
        return false;
#endif
    }
    return true;
}

static bool filterDefault(buffer *input, buffer *output) {
    int c = input->data[1]; // should be ESC, we assert it:
    LOG_DEBUG("Found ANSI sequence starting with ESC %d (%c)", c, c);
    switch (c) {
    case '7':
    case '8': // DECSC/DECRC save/restore cursor position, we just keep it.
        LOG_DEBUG("Found DECSC/DECRC sequence ESC %c, keeping it", c);
        transfer(output, input, 2);
        return filter(input, output, FILTER_DEFAULT);
    case '[': // CSI sequence.
        LOG_DEBUG("Found CSI sequence: %s", debug_buf(&quoted, *input));
        // We skip the whole ANSI sequence, which ends with a letter (A-Z or a-z) after the ESC[ and parameters.
        // get to closing 0x40 - 0x7E byte
        for (int i = 2; i < (int)input->size; i++) {
            c = input->data[i];
            if (c >= 0x40 && c <= 0x7E) {
                // found end of ANSI sequence, we skip it and filter the rest recursively
                if (c=='J') {
                    frames_count++;
                    LOG_DEBUG("Found clear screen sequence, frames count now %d", frames_count);
                    if (frames_limit > 0 && frames_count >= frames_limit) {
                        LOG_DEBUG("Reached frames limit of %d, stopping processing", frames_limit);
                        return false; // stop processing
                    }
                }
                LOG_DEBUG("Found end of ANSI sequence %c at %d, recursing", c, i);
                if (input->data[2] == '?') {
                    LOG_DEBUG("Found query sequence, removing it");
                    consume(input, i + 1);                 // remove the whole ANSI sequence from input
                    return filter(input, output, FILTER_DEFAULT); // filter the rest of the input
                }
                // Keeping color etc
                transfer(output, input, i + 1);        // transfer the whole ANSI sequence to output
                return filter(input, output, FILTER_DEFAULT); // filter the rest of the input
            }
        }
        LOG_DEBUG("Did not find end of ANSI sequence, waiting for more data to read");
        break;
    default:
        LOG_ERROR("Found other ANSI sequence starting with ESC %d %c", c, c);
#if DEBUG
        debug_print_buf(*input);
        return false;
#endif
    }
    return true;
}

int main(int argc, char **argv) {
    // Define long options
    static struct option long_options[] = {
        // long flags
        {"help", no_argument, 0, 'h'},
        {"all", no_argument, 0, 'a'},
        {"frames", required_argument, 0, 'n'},
        {"pause", no_argument, 0, 'p'},
        // terminator
        {0, 0, 0, 0}
    };
    // Parse flags using getopt_long
    int option_index = 0;
    int opt;
    filter_mode mode = FILTER_DEFAULT;
    bool pause_at_end = false;
    while ((opt = getopt_long(argc, argv, "han:p", long_options, &option_index)) != -1) {
        switch (opt) {
        case 'h':
            usage(argv[0]);
            return 0;
        case 'a':
            mode = FILTER_ALL;
            break;
        case 'n':
            frames_limit = atoi(optarg);
            break;
        case 'p':
            pause_at_end = true;
            break;
        default: // '?' for unknown option
            fprintf(stderr, "Error: unknown flag\n");
            usage(argv[0]);
            return 1;
        }
    }

    int ifile = STDIN_FILENO; // default to stdin if no file provided
    char *name = "stdin";
    if (optind < argc) {
        name = argv[optind];
        ifile = open(name, O_RDONLY);
        if (ifile < 0) {
            LOG_ERROR("Error opening input file '%s': %s", name, strerror(errno));
            return 1;
        }
    }
    LOG_INFO(
        "Filtering ANSI sequences from '%s', buf size %d, %s mode, frames limit: %d",
        name,
        BUF_SIZE,
        mode == FILTER_ALL ? "all" : "default",
        frames_limit
    );
    size_t totalRead = 0;
    size_t totalWritten = 0;
    buffer inputbuf = new_buf(BUF_SIZE);
    buffer outbuf = new_buf(BUF_SIZE);
    bool continue_processing = true;
    do {
        // Make sure we will eventually find the end (or EOF) even with tiny test buffer size.
        ssize_t n = read_n(ifile, &inputbuf, BUF_SIZE);
        if (n < 0) {
            LOG_ERROR("Error reading input: %s", strerror(errno));
            return 1;
        }
        if (n == 0) {
            break; // EOF
        }
        totalRead += n;
        LOG_DEBUG("Read %zd bytes, inputbuf now %s", n, debug_buf(&quoted, inputbuf));
        // TODO: non 'all' filter.
        continue_processing = filter(&inputbuf, &outbuf, mode);
        LOG_DEBUG("Filtered to %zd bytes %s", outbuf.size, debug_buf(&quoted, outbuf));
        ssize_t m = write_buf(1, outbuf);
        if (m < 0) {
            LOG_ERROR("Error writing output: %s", strerror(errno));
            return 1;
        }
        outbuf.size = 0; // reset output buffer for reuse
        totalWritten += m;
    } while (continue_processing);
    if (ifile != STDIN_FILENO) {
        close(ifile);
    }
    if (pause_at_end) {
        getchar();
    }
    if (frames_count < frames_limit && inputbuf.size > 0) {
        LOG_ERROR("Unterminated ANSI sequence in input buffer: %zu: %s", inputbuf.size, debug_buf(&quoted, inputbuf));
    }
    LOG_INFO("Total read: %zu bytes, total written : %zu bytes, frames processed: %d", totalRead, totalWritten, frames_count);
    free_buf(&quoted);
    free_buf(&outbuf);
    free_buf(&inputbuf);
    return 0;
}
