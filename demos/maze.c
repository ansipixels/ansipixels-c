/**
 * ansipixels-c:
 * A C library for rendering fast Terminal User Interfaces (TUIs)
 * using ANSI codes. Inspired by the Go library
 * https://pkg.go.dev/fortio.org/terminal/ansipixels
 *
 * (C) 2026 Laurent Demailly <ldemailly at gmail> and contributors.
 * Licensed under Apache-2.0 (see LICENSE).
 */

#include "ansipixels.h"
#include <stdlib.h>
#include <time.h>

enum {
    MIN_MAZE_SIZE = 2,
};

typedef struct {
    int x, y;
} point_ts;

/* Example of maze:

1 cell:

+--+
|  |
+--+

3 x 3:

    A   B   C
  +---+---+---+
1 |   |   |   |
  +---+---+---+
2 |   |   |   |
  +---+---+---+
3 |   |   |   |
  +---+---+---+

    A   B   C
  +---+---+---+
1 |           |
  +---+   +   +
2 |       |   |
  +   +---+   +
3 | S | E     |
  +---+---+---+

Representation:

with W width and H height:

Option 1)
we could store a matrix of W*Htiles/rooms/cells and in each cell redundantly indicate if there are N, S, E, W walls or
not. Option 2) we could instead store just the walls information without the redundant cell data. we have, vertical
walls: (W+1) * H possible walls (bits) horizontal walls: W * (H+1) possible walls (bits)

*/

// Cell bits: openings clockwise from north + transient + encoded next direction.
typedef enum {
    NORTH = 1 << 0,
    EAST = 1 << 1,
    SOUTH = 1 << 2,
    WEST = 1 << 3,
    VISITED = 1 << 4,
} cell_bits_e;

enum {
    WALL_MASK = NORTH | EAST | SOUTH | WEST,
    NEXT_DIR_SHIFT = 4,
    NEXT_DIR_MASK = WALL_MASK << NEXT_DIR_SHIFT,
};

static const cell_bits_e kDirs[4] = {NORTH, EAST, SOUTH, WEST};

typedef struct {
    int width;
    int height;
    uint8_t *map;
    point_ts pos;
    point_ts exit;
    cell_bits_e dir;
} maze_ts;

static inline int cell_index(const maze_ts *maze, point_ts pos) {
    return pos.y * maze->width + pos.x;
}

static inline uint8_t cell_get(const maze_ts *maze, point_ts pos) {
    return maze->map[cell_index(maze, pos)];
}

static inline void cell_and(maze_ts *maze, point_ts pos, uint8_t mask) {
    maze->map[cell_index(maze, pos)] &= mask;
}

static inline void cell_or(maze_ts *maze, point_ts pos, uint8_t bits) {
    maze->map[cell_index(maze, pos)] |= bits;
}

static bool compute_layout(const ap_t ap, bool debug_mode, int *width, int *height) {
    int max_width_compact = (ap->w - 1) / 4;
    int max_height_compact = (ap->h - 1) / 2;
    int w = max_width_compact;
    int h = max_height_compact;

    if (debug_mode) {
        int max_width_debug = ap->w / 5;
        int max_height_debug = (ap->h - 2) / 5;
        w = max_width_debug < max_width_compact ? max_width_debug : max_width_compact;
        h = max_height_debug;
    }

    if (w < MIN_MAZE_SIZE || h < MIN_MAZE_SIZE) {
        return false;
    }

    *width = w;
    *height = h;
    return true;
}

static bool ensure_maze_layout(maze_ts *maze, int width, int height) {
    if (maze->width == width && maze->height == height && maze->map != NULL) {
        return true;
    }

    uint8_t *new_map = realloc(maze->map, (size_t)width * (size_t)height * sizeof(uint8_t));
    if (!new_map) {
        return false;
    }

    maze->map = new_map;
    maze->width = width;
    maze->height = height;
    return true;
}

// prints a debug/large version of a cell (one that doesn't expect 
// walls to necessarily be symetrical, etc..)
void print_large_cell(ap_t ap, uint8_t cell, point_ts pos, int y_offset, buffer center) {
    int x = 5 * pos.x;
    int y = y_offset + 3 * pos.y;
    ap_move_to(ap, x, y);
    // corners of cells are always shown, but darker gray (so a cell with no walls still shows)
    ap_str(ap, STR("\033[90m┌\033[m"));
    // bits are showing opening/carved passages - initial 0 value is all walls.
    if (cell & NORTH) {
        ap_str(ap, STR("   "));
    } else {
        ap_str(ap, STR("───"));
    }
    ap_str(ap, STR("\033[90m┐\033[m"));
    ap_move_to(ap, x, ++y);
    if (cell & WEST) {
        ap_str(ap, STR(" "));
    } else {
        ap_str(ap, STR("│"));
    }
    append_buf(&ap->buf, center);
    if (cell & EAST) {
        ap_str(ap, STR(" "));
    } else {
        ap_str(ap, STR("│"));
    }
    ap_move_to(ap, x, ++y);
    ap_str(ap, STR("\033[90m└\033[m"));
    if (cell & SOUTH) {
        ap_str(ap, STR("   "));
    } else {
        ap_str(ap, STR("───"));
    }
    ap_str(ap, STR("\033[90m┘\033[m"));
}

string direction_arrow(cell_bits_e dir) {
    switch (dir) {
    case NORTH:
        return STR("↑");
    case EAST:
        return STR("→");
    case SOUTH:
        return STR("↓");
    case WEST:
        return STR("←");
    default:
        return STR(" ");
    }
}

static buffer styled_arrow_center(cell_bits_e dir, string style) {
    buffer center = new_buf(16);
    append_byte(&center, ' ');
    append_str(&center, style);
    append_str(&center, direction_arrow(dir));
    append_str(&center, STR("\033[m"));
    append_byte(&center, ' ');
    return center;
}

typedef struct {
    bool initialized;
    buffer empty_center;
    buffer exit_center;
    buffer start_center[4];
    buffer path_center[4];
} render_cache_ts;

static render_cache_ts g_render_cache = {0};

static void free_render_cache(void) {
    if (!g_render_cache.initialized) {
        return;
    }
    free_buf(&g_render_cache.empty_center);
    free_buf(&g_render_cache.exit_center);
    for (int i = 0; i < 4; i++) {
        free_buf(&g_render_cache.start_center[i]);
        free_buf(&g_render_cache.path_center[i]);
    }
    g_render_cache.initialized = false;
}

static void init_render_cache(void) {
    if (g_render_cache.initialized) {
        return;
    }
    atexit(free_render_cache);

    g_render_cache.empty_center = new_buf(3);
    append_str(&g_render_cache.empty_center, STR("   "));

    g_render_cache.exit_center = new_buf(3);
    append_str(&g_render_cache.exit_center, STR(" \033[41mE\033[m "));

    for (int i = 0; i < 4; i++) {
        g_render_cache.start_center[i] = styled_arrow_center(kDirs[i], STR("\033[44m"));
        g_render_cache.path_center[i] = styled_arrow_center(kDirs[i], STR("\033[30;42m"));
    }

    g_render_cache.initialized = true;
}

static inline cell_bits_e next_dir_at(const maze_ts *maze, point_ts pos) {
    return (cell_bits_e)((cell_get(maze, pos) >> NEXT_DIR_SHIFT) & WALL_MASK);
}

static void mask_all_cells(maze_ts *maze, uint8_t mask) {
    for (int y = 0; y < maze->height; y++) {
        for (int x = 0; x < maze->width; x++) {
            cell_and(maze, (point_ts){x, y}, mask);
        }
    }
}

static void print_maze_2D_debug_at(ap_t ap, maze_ts *maze, int y_offset) {
    init_render_cache();

    int start_idx = 0;
    if (maze->dir) {
        start_idx = __builtin_ctz((unsigned)maze->dir);
    }

    buffer start_center = g_render_cache.start_center[start_idx];

    for (int y = 0; y < maze->height; y++) {
        for (int x = 0; x < maze->width; x++) {
            if (x == maze->pos.x && y == maze->pos.y) {
                print_large_cell(ap, cell_get(maze, (point_ts){x, y}), (point_ts){x, y}, y_offset, start_center);
            } else if (x == maze->exit.x && y == maze->exit.y) {
                print_large_cell(ap, cell_get(maze, (point_ts){x, y}), (point_ts){x, y}, y_offset, g_render_cache.exit_center);
            } else {
                buffer center = g_render_cache.empty_center;
                cell_bits_e dir = next_dir_at(maze, (point_ts){x, y});
                if (dir) {
                    // convert back to index for bit pos.
                    int idx = __builtin_ctz((unsigned)dir);
                    center = g_render_cache.path_center[idx];
                }
                print_large_cell(ap, cell_get(maze, (point_ts){x, y}), (point_ts){x, y}, y_offset, center);
            }
        }
    }
    ap_move_to(ap, 5 * maze->pos.x + 2, y_offset + 3 * maze->pos.y + 1);
}

void print_maze_2D_debug(ap_t ap, maze_ts *maze) {
    print_maze_2D_debug_at(ap, maze, 0);
}

static inline cell_bits_e opposite(cell_bits_e dir);
static inline point_ts move(point_ts pos, cell_bits_e dir);
static inline bool in_bounds(const maze_ts *maze, point_ts p);

static inline bool is_open_between(const maze_ts *maze, point_ts pos, cell_bits_e dir) {
    point_ts adj = move(pos, dir);
    if (!in_bounds(maze, pos) || !in_bounds(maze, adj)) {
        return false;
    }
    uint8_t a = cell_get(maze, pos);
    uint8_t b = cell_get(maze, adj);
    return (a & dir) && (b & opposite(dir));
}

static inline bool has_h_wall(const maze_ts *maze, int y, int x) {
    // Horizontal segment between junction (x, y) and (x + 1, y).
    if (y <= 0 || y >= maze->height) {
        return true; // Outer border always present.
    }
    return !is_open_between(maze, (point_ts){x, y - 1}, SOUTH);
}

static inline bool has_v_wall(const maze_ts *maze, int y, int x) {
    // Vertical segment between junction (x, y) and (x, y + 1).
    if (x <= 0 || x >= maze->width) {
        return true; // Outer border always present.
    }
    return !is_open_between(maze, (point_ts){x - 1, y}, EAST);
}

static inline string junction_glyph(bool up, bool right, bool down, bool left) {
    unsigned mask = ((unsigned)up << 3) | ((unsigned)right << 2) | ((unsigned)down << 1) | (unsigned)left;
    switch (mask) {
    case 0x0:
        return STR(" ");
    case 0x1:
    case 0x4:
    case 0x5:
        return STR("─");
    case 0x2:
    case 0x8:
    case 0xA:
        return STR("│");
    case 0x3:
        return STR("┐");
    case 0x6:
        return STR("┌");
    case 0x9:
        return STR("┘");
    case 0xC:
        return STR("└");
    case 0x7:
        return STR("┬");
    case 0xB:
        return STR("┤");
    case 0xD:
        return STR("┴");
    case 0xE:
        return STR("├");
    case 0xF:
        return STR("┼");
    default:
        return STR("X");
    }
}

static void print_maze_2D_compact_at(ap_t ap, maze_ts *maze, int y_offset) {
    init_render_cache();

    int start_idx = 0;
    if (maze->dir) {
        start_idx = __builtin_ctz((unsigned)maze->dir);
    }

    buffer start_center = g_render_cache.start_center[start_idx];

    for (int y = 0; y < maze->height; y++) {
        int top_row = y_offset + 2 * y;
        int mid_row = top_row + 1;

        ap_move_to(ap, 0, top_row);
        for (int x = 0; x < maze->width; x++) {
            bool up = (y > 0) && has_v_wall(maze, y - 1, x);
            bool down = has_v_wall(maze, y, x);
            bool left = (x > 0) && has_h_wall(maze, y, x - 1);
            bool right = has_h_wall(maze, y, x);
            ap_str(ap, junction_glyph(up, right, down, left));
            if (has_h_wall(maze, y, x)) {
                ap_str(ap, STR("───"));
            } else {
                ap_str(ap, STR("   "));
            }
        }

        bool up_r = (y > 0) && has_v_wall(maze, y - 1, maze->width);
        bool down_r = has_v_wall(maze, y, maze->width);
        bool left_r = has_h_wall(maze, y, maze->width - 1);
        ap_str(ap, junction_glyph(up_r, false, down_r, left_r));

        ap_move_to(ap, 0, mid_row);
        for (int x = 0; x < maze->width; x++) {
            if (has_v_wall(maze, y, x)) {
                ap_str(ap, STR("│"));
            } else {
                ap_str(ap, STR(" "));
            }

            if (x == maze->pos.x && y == maze->pos.y) {
                append_buf(&ap->buf, start_center);
            } else if (x == maze->exit.x && y == maze->exit.y) {
                append_buf(&ap->buf, g_render_cache.exit_center);
            } else {
                buffer center = g_render_cache.empty_center;
                cell_bits_e dir = next_dir_at(maze, (point_ts){x, y});
                if (dir) {
                    int idx = __builtin_ctz((unsigned)dir);
                    center = g_render_cache.path_center[idx];
                }
                append_buf(&ap->buf, center);
            }
        }
        if (has_v_wall(maze, y, maze->width)) {
            ap_str(ap, STR("│"));
        } else {
            ap_str(ap, STR(" "));
        }
    }

    int bottom = y_offset + 2 * maze->height;
    ap_move_to(ap, 0, bottom);
    for (int x = 0; x < maze->width; x++) {
        bool up = has_v_wall(maze, maze->height - 1, x);
        bool left = (x > 0) && has_h_wall(maze, maze->height, x - 1);
        bool right = has_h_wall(maze, maze->height, x);
        ap_str(ap, junction_glyph(up, right, false, left));
        if (has_h_wall(maze, maze->height, x)) {
            ap_str(ap, STR("───"));
        } else {
            ap_str(ap, STR("   "));
        }
    }

    bool up_r = has_v_wall(maze, maze->height - 1, maze->width);
    bool left_r = has_h_wall(maze, maze->height, maze->width - 1);
    ap_str(ap, junction_glyph(up_r, false, false, left_r));

    ap_move_to(ap, 4 * maze->pos.x + 2, y_offset + 2 * maze->pos.y + 1);
}

void print_maze_2D_compact(ap_t ap, maze_ts *maze) {
    print_maze_2D_compact_at(ap, maze, 0);
}

static inline cell_bits_e opposite(cell_bits_e dir) {
    // Rotate 4 direction bits by 2: N<->S, E<->W
    return (cell_bits_e)(((unsigned)dir << 2 | (unsigned)dir >> 2) & 0x0F);
}

static inline point_ts move(point_ts pos, cell_bits_e dir) {
    pos.x += (dir == EAST) - (dir == WEST);
    pos.y += (dir == SOUTH) - (dir == NORTH);
    return pos;
}

static inline bool in_bounds(const maze_ts *maze, point_ts p) {
    return p.x >= 0 && p.x < maze->width && p.y >= 0 && p.y < maze->height;
}

static inline bool same_point(point_ts a, point_ts b) {
    return a.x == b.x && a.y == b.y;
}

static inline void clear_next_dir(maze_ts *maze, point_ts pos) {
    cell_and(maze, pos, (uint8_t)~NEXT_DIR_MASK);
}

static inline void set_next_dir(maze_ts *maze, point_ts pos, cell_bits_e dir) {
    clear_next_dir(maze, pos);
    cell_or(maze, pos, (uint8_t)((dir & WALL_MASK) << NEXT_DIR_SHIFT));
}

point_ts open_wall(maze_ts *maze, point_ts pos, cell_bits_e dir) {
    cell_or(maze, pos, (uint8_t)dir);
    // Also open the opposite wall in the adjacent cell
    point_ts adj_pos = move(pos, dir);
    if (in_bounds(maze, adj_pos)) {
        cell_or(maze, adj_pos, (uint8_t)opposite(dir));
    }
    return adj_pos;
}

static inline void shuffle_directions(cell_bits_e dirs[4]) {
    for (int i = 3; i > 0; i--) {
        int j = rand() % (i + 1);
        cell_bits_e tmp = dirs[i];
        dirs[i] = dirs[j];
        dirs[j] = tmp;
    }
}

static bool mark_path_dfs(maze_ts *maze, point_ts cur, point_ts prev, point_ts exit) {
    if (cur.x == exit.x && cur.y == exit.y) {
        clear_next_dir(maze, cur);
        return true;
    }
    for (int i = 0; i < 4; i++) {
        cell_bits_e dir = kDirs[i];
        point_ts next = move(cur, dir);
        if (!in_bounds(maze, next) || same_point(next, prev)) {
            continue;
        }
        if (!(cell_get(maze, cur) & dir)) {
            continue;
        }
        if (mark_path_dfs(maze, next, cur, exit)) {
            set_next_dir(maze, cur, dir);
            return true;
        }
    }
    return false;
}

void mark_solution_path(maze_ts *maze, point_ts start, point_ts exit) {
    maze->dir = NORTH;
    // This function is intended to run only after carving; drop any stale high-bit metadata.
    mask_all_cells(maze, WALL_MASK);
    if (in_bounds(maze, start) && in_bounds(maze, exit)) {
        (void)mark_path_dfs(maze, start, (point_ts){-1, -1}, exit);
        maze->dir = next_dir_at(maze, start);
        if (!maze->dir) {
            maze->dir = NORTH;
        }
    }
}

void carve_maze(maze_ts *maze, point_ts start, point_ts exit) {
    point_ts *stack = calloc((size_t)maze->width * (size_t)maze->height, sizeof(*stack));
    if (!stack) {
        return;
    }
    int top = 0;
    if (!in_bounds(maze, start)) {
        start = (point_ts){0, maze->height - 1};
    }
    if (!in_bounds(maze, exit)) {
        exit = (point_ts){maze->width - 1, 0};
    }
    memset(maze->map, 0, (size_t)maze->width * (size_t)maze->height);
    maze->pos = start;
    maze->exit = exit;
    cell_or(maze, start, VISITED);
    stack[top++] = start;
    while (top > 0) {
        point_ts cur = stack[top - 1];
        cell_bits_e dirs[4] = {kDirs[0], kDirs[1], kDirs[2], kDirs[3]};
        bool carved = false;

        shuffle_directions(dirs);
        for (int i = 0; i < 4; i++) {
            point_ts next = move(cur, dirs[i]);
            if (!in_bounds(maze, next) || (cell_get(maze, next) & VISITED)) {
                continue;
            }
            open_wall(maze, cur, dirs[i]);
            cell_or(maze, next, VISITED);
            stack[top++] = next;
            carved = true;
            break;
        }
        if (!carved) {
            top--;
        }
    }
    mask_all_cells(maze, WALL_MASK);
    free(stack);
}

int main(int argc, char *argv[]) {
    srand((unsigned)time(NULL));
    ap_t ap = ap_open();
    if (!ap) {
        return 1; // error already logged
    }
    bool debug_mode = (argc > 1) && (strcmp(argv[1], "-debug") == 0);

    int width = 0;
    int height = 0;
    if (!compute_layout(ap, debug_mode, &width, &height)) {
        dprintf(STDERR_FILENO, "Terminal too small for maze mode (%s): %dx%d\n", debug_mode ? "-debug" : "compact", ap->w,
                ap->h);
        return 1;
    }

    maze_ts maze = {0};
    if (!ensure_maze_layout(&maze, width, height)) {
        dprintf(STDERR_FILENO, "Failed to allocate maze map (%dx%d)\n", width, height);
        return 1;
    }

    // one of the corners so every direction is available
    point_ts start = {1, maze.height - 2};
    point_ts exit = {maze.width - 2, 1};
    int k = 0;
    do {
        if (ap->resized) {
            ap->resized = false;
            if (!compute_layout(ap, debug_mode, &width, &height)) {
                ap_start(ap);
                ap_clear_screen(ap, false);
                ap_move_to(ap, 0, 0);
                ap_str(ap, STR("Terminal too small - resize and press any key (q to quit)."));
                ap_end(ap);
                char ch = 0;
                ssize_t n;
                for (;;) {
                    n = read(STDIN_FILENO, &ch, 1);
                    if (n < 0 && errno == EINTR) {
                        // Resize should immediately re-run layout logic without needing a key.
                        break;
                    }
                    if (n <= 0 || ch == 'q' || ch == 3) {
                        k = (n <= 0) ? EOF : (unsigned char)ch;
                    }
                    break;
                }
                if (n <= 0 || ch == 'q' || ch == 3) {
                    break;
                }
                continue;
            }
            if (!ensure_maze_layout(&maze, width, height)) {
                dprintf(STDERR_FILENO, "Failed to resize maze map (%dx%d)\n", width, height);
                break;
            }
            start = (point_ts){1, maze.height - 2};
            exit = (point_ts){maze.width - 2, 1};
        }

        carve_maze(&maze, start, exit);
        mark_solution_path(&maze, start, exit);
        ap_start(ap);
        ap_clear_screen(ap, false);
        if (debug_mode) {
            print_maze_2D_debug_at(ap, &maze, 0);
            print_maze_2D_compact_at(ap, &maze, 3 * maze.height + 1);
        } else {
            print_maze_2D_compact(ap, &maze);
        }
        ap_end(ap);

        char ch = 0;
        ssize_t n;
        for (;;) {
            n = read(STDIN_FILENO, &ch, 1);
            if (n < 0 && errno == EINTR) {
                // SIGWINCH interrupted read: treat as synthetic event and repaint.
                k = 0;
                break;
            }
            break;
        }

        if (n < 0) {
            if (errno != EINTR) {
                dprintf(STDERR_FILENO, "Error reading input: %s\n", strerror(errno));
                break;
            }
        }
        if (n == 0) {
            k = EOF;
        } else if (n > 0) {
            k = (unsigned char)ch;
        }
    } while (k != 'q' && k != 3 && k != EOF);
    if (debug_mode) {
        ap_move_to(ap, 0, 5 * maze.height + 2);
    } else {
        ap_move_to(ap, 0, 2 * maze.height + 1);
    }

    free(maze.map);
}
