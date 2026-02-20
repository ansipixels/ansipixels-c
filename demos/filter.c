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
#include <sys/wait.h>
#include <unistd.h>

enum {
#if DEBUG
    // short on purpose for testing to trigger potential bugs with half complete sequences.
    BUF_SIZE = 4
#else
    BUF_SIZE = 1 << 16
#endif
};

static buffer quoted = {0};

void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [flags] [filename or stdin]\n", prog);
    fprintf(stderr, "Flags:\n");
    fprintf(stderr, "  -h, --help       show this help message\n");
    fprintf(stderr, "  -n, --frames <n> stop after filtering n frames (clear screens)\n");
    fprintf(stderr, "  -a, --all        filters all ANSI sequences, leaving only the text content\n");
    fprintf(stderr, "  -p, --pause      pause at the end (implies raw mode for filter itself and a filename)\n");
}

typedef enum filter_mode {
    FILTER_DEFAULT, // only filter query and mode settings sequences
    FILTER_ALL      // filter all ANSI sequences, leaving only the text content
} filter_mode;

// Returns >0 if the clear screen sequence was found (ending at that
// returned position, not transferred to output), otherwise 0.
int filter(buffer *input, buffer *output, filter_mode mode, bool eof) {
    while (true) {
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
        // part before first escape character, possibly all of it if no escape character found
        transfer(output, input, n);
        if (esc == NULL) {
            // No ANSI sequences, we just copied input to output, nothing left in input.
            LOG_DEBUG("No ANSI sequence found, transferred all %zu bytes to output", n);
            assert(input->size == 0);
            return 0;
        }
        LOG_DEBUG("Input post transfer is now %s", debug_buf(&quoted, *input));
        if (input->size < 3) {
            LOG_DEBUG("Not enough data to contain a full ANSI sequence, waiting for more data to read");
            // Not enough data to contain a full ANSI sequence, wait for more data to read.
            // if we found no (new) data and we reached EOF, it's an error, otherwise just wait for more data.
            return eof ? -1 : 0;
        }
        int c = input->data[1]; // should be ESC, we assert it:
        LOG_DEBUG("Found ANSI sequence starting with ESC %d (%c)", c, c);
        switch (c) {
        case '>':
        case '=': // DECPAM/DECPNM we ignore in all modes.
            LOG_DEBUG("Found DECPAM/DECPNM sequence ESC %c", c);
            consume(input, 2); // remove ESC and the > or = byte
            continue;
        case '7':
        case '8': // DECSC/DECRC save/restore cursor position.
            LOG_DEBUG("Found DECSC/DECRC sequence ESC %c", c);
            if (mode == FILTER_ALL) {
                consume(input, 2); // remove ESC and the 7/8 byte
            } else {
                transfer(output, input, 2);
            }
            continue;
        case '[': // CSI sequence.
            LOG_DEBUG("Found CSI sequence: %s", debug_buf(&quoted, *input));
            // CSI ends at a final byte in the 0x40..0x7E range.
            for (int i = 2; i < (int)input->size; i++) {
                c = input->data[i];
                if (c >= 0x40 && c <= 0x7E) {
                    char start = input->data[2];
                    LOG_DEBUG("Found end of ANSI sequence %c, starts %c at %d, continuing", c, start, i);
                    if (c == 'J') {
                        return i + 1;
                    }
                    // TODO: Would strncmp like of ?2026 be faster/better?
                    if (mode == FILTER_DEFAULT && c != 'n' && c != 'c' && c != 'u' &&
                        (start != '?' || (i == 7 && (c == 'h' || c == 'l') && input->data[3] == '2' &&
                                          input->data[4] == '0' && input->data[5] == '2' && input->data[6] == '6'))) {
                        // Keep non-query non status non kitty CSI in default mode (for colors/cursor moves).
                        // And do also keep \033[?2026h and \033[?2026l (avoids flickering).
                        transfer(output, input, i + 1);
                    } else {
                        // Drop all CSI in all-mode and query CSI in default mode.
                        consume(input, i + 1);
                    }
                    goto next_iteration;
                }
            }
            LOG_DEBUG("Did not find end of CSI sequence, waiting for more data to read eof=%d", eof);
            return eof ? -1 : 0;
        case ']': // OSC sequence, yank it until BEL or ST (ESC \)
            LOG_DEBUG("Found OSC sequence: %s", debug_buf(&quoted, *input));
            for (int i = 2; i < (int)input->size; i++) {
                c = input->data[i];
                if (c == '\a' || (c == '\\' && input->data[i - 1] == '\x1b')) {
                    LOG_DEBUG("Found end of OSC sequence at %d, continuing", i);
                    consume(input, i + 1);
                    goto next_iteration;
                }
            }
            LOG_DEBUG("Did not find end of OSC sequence, waiting for more data to read");
            return eof ? -1 : 0;
        case 'P': // DCS sequence, yank until ST (ESC \)
            LOG_DEBUG("Found DCS sequence: %s", debug_buf(&quoted, *input));
            for (int i = 2; i < (int)input->size; i++) {
                c = input->data[i];
                if (c == '\\' && input->data[i - 1] == '\x1b') {
                    LOG_DEBUG("Found end of DCS sequence at %d, continuing", i);
                    consume(input, i + 1);
                    goto next_iteration;
                }
            }
            LOG_DEBUG("Did not find end of DCS sequence, waiting for more data to read");
            return eof ? -1 : 0;
        case '(':
        case ')': // SCS sequence, we ignore it in all modes, it's followed by an extra character.
            LOG_DEBUG("Found SCS sequence ESC %c", c);
            consume(input, 3); // remove ESC and the ( or ) byte
            continue;
        default:
            LOG_ERROR("Found other ANSI sequence starting with ESC %d %c - please report a bug", c, c);
            debug_print_buf(*input);
            return -1;
        }
    next_iteration:
        continue;
    }
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
    int frames_limit = -1; // default to no frame limit

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
    ap_t ap = NULL;
    if (optind < argc) {
        name = argv[optind];
        ifile = open(name, O_RDONLY);
        if (ifile < 0) {
            LOG_ERROR("Error opening input file '%s': %s", name, strerror(errno));
            return 1;
        }
        if (pause_at_end) {
            ap = ap_open();
            if (ap == NULL) {
                LOG_ERROR("Error opening ansipixels instance for pause at end: %s", strerror(errno));
                return 1;
            }
            ap_hide_cursor(ap); // atexit will restore.
            ap_clear_screen(ap, false);
            ap_flush(ap);
        }
    } else if (pause_at_end) {
        LOG_ERROR("%s: Pause at end flag requires input as a file, cannot be used with stdin", argv[0]);
        usage(argv[0]);
        return 1;
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
    int new_frame_end_index = 0;
    int frames_count = 0;
    buffer stdin_buf = new_buf(BUF_SIZE);
    bool eof = false;
    do {
        // Make sure we will eventually find the end (or EOF) even with tiny test buffer size.
        ssize_t n = read_n(ifile, &inputbuf, BUF_SIZE);
        if (n < 0) {
            LOG_ERROR("Error reading input: %s", strerror(errno));
            return 1;
        }
        if (n == 0) {
            eof = true;
            if (inputbuf.size == 0) {
                continue_processing = false; // EOF
                goto pause_check;
            }
        }
        totalRead += n;
        LOG_DEBUG("Read %zd bytes, inputbuf now %s", n, debug_buf(&quoted, inputbuf));
        new_frame_end_index = filter(&inputbuf, &outbuf, mode, eof);
        if (new_frame_end_index < 0) {
            continue_processing = false;
        } else if (new_frame_end_index > 0) {
            frames_count++;
            LOG_DEBUG("Found clear screen sequence offset %d, frames count now %d", new_frame_end_index, frames_count);
            if (frames_limit > 0 && frames_count >= frames_limit) {
                LOG_DEBUG("Reached frames limit of %d, stopping processing", frames_limit);
                continue_processing = false;
            }
        }
        LOG_DEBUG("Filtered to %zd bytes %s", outbuf.size, debug_buf(&quoted, outbuf));
        ssize_t m = write_buf(1, outbuf);
        if (m < 0) {
            LOG_ERROR("Error writing output: %s", strerror(errno));
            return 1;
        }
        outbuf.size = 0; // reset output buffer for reuse
        totalWritten += m;
        if (new_frame_end_index > 0) {
            LOG_DEBUG("Outputting filtered clear screen sequence and text content until next frame");
            if (mode == FILTER_ALL) {
                // remove the clear screen sequence (filter all).
                consume(&inputbuf, new_frame_end_index);
            } else {
                // add that clear screen to the output buffer for next iteration.
                transfer(&outbuf, &inputbuf, new_frame_end_index);
            }
        }
    pause_check:
        if (pause_at_end) {
            // Check for Ctrl-C or Ctrl-D without blocking.
            if (continue_processing && ap_stdin_ready(ap)) {
                ssize_t stdin_n =
                    read_buf(STDIN_FILENO, &stdin_buf); // read input only when select() says data is ready
                if (stdin_n > 0) {
                    LOG_DEBUG("Read %zd bytes: %s", stdin_n, debug_buf(&quoted, stdin_buf));
                    if (stdin_buf.data[0] == '\x03' || stdin_buf.data[0] == '\x04') { // Ctrl-C or Ctrl-D
                        ap_move_to(ap, 0, 0);
                        ap_str(ap, STR(RED));
                        ap_str(ap, STR("Exit input request received, exiting..."));
                        ap_str(ap, STR(RESET));
                        ap_end(ap);
                        return 1;
                    }
                    stdin_buf.size = 0; // reset input buffer for reuse
                }
            }
            // Pause at the end (!continued_processing) or if we hit a new frame.
            if (!continue_processing || new_frame_end_index > 0) {
                ap_show_cursor(ap);
                ap_end(ap);
                read_buf(STDIN_FILENO, &stdin_buf); // wait for any input to exit
                ap_hide_cursor(ap);
                ap_flush(ap);
                stdin_buf.size = 0; // reset input buffer for reuse
            }
        }
    } while (continue_processing);
    if (ifile != STDIN_FILENO) {
        close(ifile);
    }
    // always report when no frames_limit or we stopped before the limit.
    if (inputbuf.size > 0 && frames_count != frames_limit) {
        LOG_ERROR(
            "Unterminated ANSI sequence in input buffer: %zu: %s",
            inputbuf.size,
            debug_buf(&quoted, slice_buf(inputbuf, 0, 20))
        );
    }
    LOG_INFO("Total read: %zu bytes, written : %zu bytes, frames processed: %d", totalRead, totalWritten, frames_count);
    free_buf(&quoted);
    free_buf(&outbuf);
    free_buf(&inputbuf);
    return 0;
}
