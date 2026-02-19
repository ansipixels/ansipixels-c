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
    int c = input->data[1]; // should be ESC, we assert it:
    LOG_DEBUG("Found ANSI sequence starting with ESC %d (%c)", c, c);
    switch (c) {
    case '7':
    case '8': // DECSC/DECRC save/restore cursor position.
        LOG_DEBUG("Found DECSC/DECRC sequence ESC %c", c);
        if (mode == FILTER_ALL) {
            consume(input, 2); // remove ESC and the 7/8 byte
        } else {
            transfer(output, input, 2);
        }
        return filter(input, output, mode);
    case '[': // CSI sequence.
        LOG_DEBUG("Found CSI sequence: %s", debug_buf(&quoted, *input));
        // CSI ends at a final byte in the 0x40..0x7E range.
        for (int i = 2; i < (int)input->size; i++) {
            c = input->data[i];
            if (c >= 0x40 && c <= 0x7E) {
                if (c == 'J') {
                    frames_count++;
                    LOG_DEBUG("Found clear screen sequence, frames count now %d", frames_count);
                    if (frames_limit > 0 && frames_count >= frames_limit) {
                        LOG_DEBUG("Reached frames limit of %d, stopping processing", frames_limit);
                        return false; // stop processing
                    }
                }
                char start = input->data[2];
                LOG_DEBUG("Found end of ANSI sequence %c, starts %c at %d, recursing", c, start, i);
                if (mode == FILTER_DEFAULT && start != '?' && c != 'n' && c != 'c') {
                    // Keep non-query or status CSI in default mode (for colors/cursor moves).
                    transfer(output, input, i + 1);
                } else {
                    // Drop all CSI in all-mode and query CSI in default mode.
                    consume(input, i + 1);
                }
                return filter(input, output, mode);
            }
        }
        LOG_DEBUG("Did not find end of CSI sequence, waiting for more data to read");
        break;
    case ']': // OSC sequence, yank it until BEL or ST (ESC \)
        LOG_DEBUG("Found OSC sequence: %s", debug_buf(&quoted, *input));
        for (int i = 2; i < (int)input->size; i++) {
            c = input->data[i];
            if (c == '\a' || (c == '\\' && i > 2 && input->data[i - 1] == '\x1b')) {
                LOG_DEBUG("Found end of OSC sequence at %d, recursing", i);
                consume(input, i + 1);
                return filter(input, output, mode);
            }
        }
        LOG_DEBUG("Did not find end of OSC sequence, waiting for more data to read");
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
    LOG_INFO("Total read: %zu bytes, written : %zu bytes, frames processed: %d", totalRead, totalWritten, frames_count);
    free_buf(&quoted);
    free_buf(&outbuf);
    free_buf(&inputbuf);
    return 0;
}
