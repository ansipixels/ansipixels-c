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
    // MAZE_SIZE = 20,
    MAZE_WIDTH = 16, // for 80 cols
    MAZE_HEIGHT = 8, // for 24 or 25 lines.
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
    uint8_t map[MAZE_HEIGHT][MAZE_WIDTH];
    point_ts pos;
    point_ts exit;
    cell_bits_e dir;
} maze_ts;

void print_cell(ap_t ap, uint8_t cell, point_ts pos, buffer center) {
    int x = 5 * pos.x;
    int y = 3 * pos.y;
    ap_move_to(ap, x, y);
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

static void init_render_cache(void) {
    if (g_render_cache.initialized) {
        return;
    }

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
    return (cell_bits_e)((maze->map[pos.y][pos.x] >> NEXT_DIR_SHIFT) & WALL_MASK);
}

static void mask_all_cells(maze_ts *maze, uint8_t mask) {
    for (int y = 0; y < MAZE_HEIGHT; y++) {
        for (int x = 0; x < MAZE_WIDTH; x++) {
            maze->map[y][x] &= mask;
        }
    }
}

void print_maze_2D(ap_t ap, maze_ts *maze) {
    init_render_cache();

    int start_idx = 0;
    if (maze->dir) {
        start_idx = __builtin_ctz((unsigned)maze->dir);
    }

    buffer start_center = g_render_cache.start_center[start_idx];

    for (int y = 0; y < MAZE_HEIGHT; y++) {
        for (int x = 0; x < MAZE_WIDTH; x++) {
            if (x == maze->pos.x && y == maze->pos.y) {
                print_cell(ap, maze->map[y][x], (point_ts){x, y}, start_center);
            } else if (x == maze->exit.x && y == maze->exit.y) {
                print_cell(ap, maze->map[y][x], (point_ts){x, y}, g_render_cache.exit_center);
            } else {
                buffer center = g_render_cache.empty_center;
                cell_bits_e dir = next_dir_at(maze, (point_ts){x, y});
                if (dir) {
                    // convert back to index for bit pos.
                    int idx = __builtin_ctz((unsigned)dir);
                    center = g_render_cache.path_center[idx];
                }
                print_cell(ap, maze->map[y][x], (point_ts){x, y}, center);
            }
        }
    }
    ap_move_to(ap, 5 * maze->pos.x + 2, 3 * maze->pos.y + 1);
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

static inline bool in_bounds(point_ts p) {
    return p.x >= 0 && p.x < MAZE_WIDTH && p.y >= 0 && p.y < MAZE_HEIGHT;
}

static inline bool same_point(point_ts a, point_ts b) {
    return a.x == b.x && a.y == b.y;
}

static inline void clear_next_dir(maze_ts *maze, point_ts pos) {
    maze->map[pos.y][pos.x] &= (uint8_t)~NEXT_DIR_MASK;
}

static inline void set_next_dir(maze_ts *maze, point_ts pos, cell_bits_e dir) {
    clear_next_dir(maze, pos);
    maze->map[pos.y][pos.x] |= (uint8_t)((dir & WALL_MASK) << NEXT_DIR_SHIFT);
}

point_ts open_wall(maze_ts *maze, point_ts pos, cell_bits_e dir) {
    maze->map[pos.y][pos.x] |= dir;
    // Also open the opposite wall in the adjacent cell
    point_ts adj_pos = move(pos, dir);
    if (in_bounds(adj_pos)) {
        maze->map[adj_pos.y][adj_pos.x] |= opposite(dir);
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
        if (!in_bounds(next) || same_point(next, prev)) {
            continue;
        }
        if (!(maze->map[cur.y][cur.x] & dir)) {
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
    if (in_bounds(start) && in_bounds(exit)) {
        (void)mark_path_dfs(maze, start, (point_ts){-1, -1}, exit);
        maze->dir = next_dir_at(maze, start);
        if (!maze->dir) {
            maze->dir = NORTH;
        }
    }
}

void carve_maze(maze_ts *maze, point_ts start, point_ts exit) {
    point_ts stack[MAZE_WIDTH * MAZE_HEIGHT];
    int top = 0;
    if (!in_bounds(start)) {
        start = (point_ts){0, MAZE_HEIGHT - 1};
    }
    if (!in_bounds(exit)) {
        exit = (point_ts){MAZE_WIDTH - 1, 0};
    }
    memset(maze->map, 0, sizeof(maze->map));
    maze->pos = start;
    maze->exit = exit;
    maze->map[start.y][start.x] |= VISITED;
    stack[top++] = start;
    while (top > 0) {
        point_ts cur = stack[top - 1];
        cell_bits_e dirs[4] = {kDirs[0], kDirs[1], kDirs[2], kDirs[3]};
        bool carved = false;

        shuffle_directions(dirs);
        for (int i = 0; i < 4; i++) {
            point_ts next = move(cur, dirs[i]);
            if (!in_bounds(next) || (maze->map[next.y][next.x] & VISITED)) {
                continue;
            }
            open_wall(maze, cur, dirs[i]);
            maze->map[next.y][next.x] |= VISITED;
            stack[top++] = next;
            carved = true;
            break;
        }
        if (!carved) {
            top--;
        }
    }
    mask_all_cells(maze, WALL_MASK);
}

int main(void) {
    srand((unsigned)time(NULL));
    ap_t ap = ap_open();
    if (!ap) {
        return 1; // error already logged
    }
    maze_ts maze = {0};
    // one of the corners so every direction is available
    point_ts start = {1, MAZE_HEIGHT - 2};
    point_ts exit = {MAZE_WIDTH - 2, 1};
    int k;
    do {
        carve_maze(&maze, start, exit);
        mark_solution_path(&maze, start, exit);
        ap_start(ap);
        ap_clear_screen(ap, false);
        print_maze_2D(ap, &maze);
        ap_end(ap);
        k = getchar();
    } while (k != 'q' && k != 3 && k != EOF);
    ap_move_to(ap, 0, 3 * MAZE_HEIGHT);
}
