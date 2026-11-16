// Minesweeper in C++ with SDL2
// Controls:
//   Left click  = reveal a square (click a number to reveal its neighbors
//                 when you've placed the right number of flags around it)
//   Right click = place / remove a flag
//   R = restart,  1 = Easy,  2 = Medium,  3 = Hard,  Esc = quit
//
// Build: g++ minesweeper.cpp -o minesweeper $(sdl2-config --cflags --libs)

#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>

const int CS = 32; // cell size in pixels

struct Cell {
    bool mine = false, revealed = false, flagged = false;
    int adj = 0;
};

enum State { PLAYING, WON, LOST };

struct Difficulty { int cols, rows, mines; const char *name; };
const Difficulty LEVELS[3] = {
    {9, 9, 10, "Easy"},
    {16, 16, 40, "Medium"},
    {30, 16, 99, "Hard"},
};

// Tiny 3x5 pixel font for the digits 1-8 (each row is 3 bits)
const int FONT[9][5] = {
    {0, 0, 0, 0, 0},
    {2, 6, 2, 2, 7}, // 1
    {7, 1, 7, 4, 7}, // 2
    {7, 1, 7, 1, 7}, // 3
    {5, 5, 7, 1, 1}, // 4
    {7, 4, 7, 1, 7}, // 5
    {7, 4, 7, 5, 7}, // 6
    {7, 1, 1, 1, 1}, // 7
    {7, 5, 7, 5, 7}, // 8
};

const SDL_Color NUM_COLORS[9] = {
    {0, 0, 0, 255},       {30, 80, 220, 255},  {20, 130, 40, 255},
    {210, 40, 40, 255},   {20, 20, 140, 255},  {140, 20, 20, 255},
    {20, 140, 140, 255},  {20, 20, 20, 255},   {110, 110, 110, 255},
};

struct Game {
    int cols, rows, mines, level = 0;
    std::vector<Cell> cells;
    State state = PLAYING;
    bool started = false;      // becomes true on the first click
    Uint32 startTime = 0, endTime = 0;
    int flags = 0, revealedCount = 0;

    Cell &at(int c, int r) { return cells[r * cols + c]; }
    bool inside(int c, int r) const { return c >= 0 && r >= 0 && c < cols && r < rows; }

    void reset(int lvl) {
        level = lvl;
        cols = LEVELS[lvl].cols;
        rows = LEVELS[lvl].rows;
        mines = LEVELS[lvl].mines;
        cells.assign(cols * rows, Cell());
        state = PLAYING;
        started = false;
        flags = revealedCount = 0;
    }

    // Place mines AFTER the first click so the first click is always safe
    void placeMines(int safeC, int safeR) {
        int placed = 0;
        while (placed < mines) {
            int c = rand() % cols, r = rand() % rows;
            if (std::abs(c - safeC) <= 1 && std::abs(r - safeR) <= 1) continue;
            if (at(c, r).mine) continue;
            at(c, r).mine = true;
            placed++;
        }
        for (int r = 0; r < rows; r++)
            for (int c = 0; c < cols; c++) {
                int n = 0;
                for (int dr = -1; dr <= 1; dr++)
                    for (int dc = -1; dc <= 1; dc++)
                        if ((dr || dc) && inside(c + dc, r + dr) && at(c + dc, r + dr).mine) n++;
                        at(c, r).adj = n;
            }
    }

    void lose() {
        state = LOST;
        endTime = SDL_GetTicks();
        for (auto &cell : cells)
            if (cell.mine) cell.revealed = true;
    }

    void checkWin() {
        if (revealedCount == cols * rows - mines) {
            state = WON;
            endTime = SDL_GetTicks();
            for (auto &cell : cells)
                if (cell.mine) cell.flagged = true;
                flags = mines;
        }
    }

    // Reveal a cell. Empty cells (0 neighbors) spread out to open their neighbors too.
    void reveal(int c, int r) {
        if (!inside(c, r) || state != PLAYING) return;
        Cell &start = at(c, r);
        if (start.revealed || start.flagged) return;

        if (!started) {
            started = true;
            startTime = SDL_GetTicks();
            placeMines(c, r);
        }
        if (start.mine) { start.revealed = true; lose(); return; }

        std::vector<std::pair<int, int>> stack;
        stack.push_back({c, r});
        while (!stack.empty()) {
            auto [cc, rr] = stack.back();
            stack.pop_back();
            if (!inside(cc, rr)) continue;
            Cell &cell = at(cc, rr);
            if (cell.revealed || cell.flagged || cell.mine) continue;
            cell.revealed = true;
            revealedCount++;
            if (cell.adj == 0)
                for (int dr = -1; dr <= 1; dr++)
                    for (int dc = -1; dc <= 1; dc++)
                        if (dr || dc) stack.push_back({cc + dc, rr + dr});
        }
        checkWin();
    }

    void toggleFlag(int c, int r) {
        if (!inside(c, r) || state != PLAYING) return;
        Cell &cell = at(c, r);
        if (cell.revealed) return;
        cell.flagged = !cell.flagged;
        flags += cell.flagged ? 1 : -1;
    }

    // Click a revealed number that has enough flags around it to open the rest
    void chord(int c, int r) {
        if (!inside(c, r) || state != PLAYING) return;
        Cell &cell = at(c, r);
        if (!cell.revealed || cell.adj == 0) return;
        int f = 0;
        for (int dr = -1; dr <= 1; dr++)
            for (int dc = -1; dc <= 1; dc++)
                if ((dr || dc) && inside(c + dc, r + dr) && at(c + dc, r + dr).flagged) f++;
                if (f != cell.adj) return;
                for (int dr = -1; dr <= 1; dr++)
                    for (int dc = -1; dc <= 1; dc++)
                        if (dr || dc) reveal(c + dc, r + dr);
    }

    int seconds() const {
        if (!started) return 0;
        Uint32 end = (state == PLAYING) ? SDL_GetTicks() : endTime;
        return (int)((end - startTime) / 1000);
    }
};

static void fillRect(SDL_Renderer *ren, int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderFillRect(ren, &rc);
}

static void drawDigit(SDL_Renderer *ren, int n, int cx, int cy) {
    const int S = 4; // each font pixel is 4x4 screen pixels
    int ox = cx + (CS - 3 * S) / 2, oy = cy + (CS - 5 * S) / 2;
    for (int row = 0; row < 5; row++)
        for (int col = 0; col < 3; col++)
            if (FONT[n][row] & (4 >> col))
                fillRect(ren, ox + col * S, oy + row * S, S, S, NUM_COLORS[n]);
}

static void drawFlag(SDL_Renderer *ren, int x, int y) {
    SDL_Color pole = {30, 30, 30, 255}, red = {220, 40, 40, 255};
    fillRect(ren, x + 15, y + 8, 3, 16, pole);   // pole
    fillRect(ren, x + 9, y + 23, 15, 3, pole);   // base
    for (int i = 0; i < 6; i++) {                // triangle flag, pointing left
        int w = 12 - i * 2;
        fillRect(ren, x + 16 - w, y + 7 + i * 2, w, 2, red);
    }
}

static void drawMine(SDL_Renderer *ren, int x, int y, bool exploded) {
    if (exploded) fillRect(ren, x, y, CS, CS, {230, 60, 60, 255});
    SDL_Color k = {20, 20, 20, 255};
    fillRect(ren, x + 9, y + 9, 14, 14, k);   // body
    fillRect(ren, x + 15, y + 5, 2, 22, k);   // spikes
    fillRect(ren, x + 5, y + 15, 22, 2, k);
    fillRect(ren, x + 11, y + 11, 4, 4, {240, 240, 240, 255}); // shine
}

int main(int, char **) {
    srand((unsigned)time(nullptr));
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    Game g;
    g.reset(0);
    SDL_Window *win = SDL_CreateWindow("Minesweeper", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       g.cols * CS, g.rows * CS, SDL_WINDOW_SHOWN);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!win || !ren) {
        std::printf("Window/renderer failed: %s\n", SDL_GetError());
        return 1;
    }

    auto newGame = [&](int lvl) {
        g.reset(lvl);
        SDL_SetWindowSize(win, g.cols * CS, g.rows * CS);
        SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    };

    bool running = true;
    while (running) {
        SDL_Event e;
        // Wait up to 100ms for an event so the timer in the title still updates
        if (SDL_WaitEventTimeout(&e, 100)) {
            do {
                if (e.type == SDL_QUIT) running = false;
                if (e.type == SDL_KEYDOWN) {
                    switch (e.key.keysym.sym) {
                        case SDLK_ESCAPE: running = false; break;
                        case SDLK_r: newGame(g.level); break;
                        case SDLK_1: newGame(0); break;
                        case SDLK_2: newGame(1); break;
                        case SDLK_3: newGame(2); break;
                    }
                }
                if (e.type == SDL_MOUSEBUTTONDOWN) {
                    int c = e.button.x / CS, r = e.button.y / CS;
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        if (g.inside(c, r) && g.at(c, r).revealed) g.chord(c, r);
                        else g.reveal(c, r);
                    } else if (e.button.button == SDL_BUTTON_RIGHT) {
                        g.toggleFlag(c, r);
                    }
                }
            } while (SDL_PollEvent(&e) && running);
        }

        // ---- Title bar shows status ----
        char title[160];
        const char *status = g.state == WON ? "  YOU WIN!  (R = restart)"
        : g.state == LOST ? "  BOOM!  (R = restart)" : "";
        std::snprintf(title, sizeof title, "Minesweeper %s | Mines left: %d | Time: %d%s | 1/2/3 = difficulty",
                      LEVELS[g.level].name, g.mines - g.flags, g.seconds(), status);
        SDL_SetWindowTitle(win, title);

        // ---- Draw ----
        SDL_SetRenderDrawColor(ren, 90, 90, 100, 255);
        SDL_RenderClear(ren);

        for (int r = 0; r < g.rows; r++)
            for (int c = 0; c < g.cols; c++) {
                Cell &cell = g.at(c, r);
                int x = c * CS, y = r * CS;
                if (cell.revealed) {
                    fillRect(ren, x + 1, y + 1, CS - 2, CS - 2, {205, 205, 210, 255});
                    if (cell.mine) drawMine(ren, x, y, g.state == LOST && !cell.flagged);
                    else if (cell.adj > 0) drawDigit(ren, cell.adj, x, y);
                } else {
                    fillRect(ren, x + 1, y + 1, CS - 2, CS - 2, {150, 160, 185, 255});
                    fillRect(ren, x + 1, y + 1, CS - 2, 3, {190, 200, 225, 255});   // light top edge
                    fillRect(ren, x + 1, y + CS - 4, CS - 2, 3, {105, 115, 140, 255}); // dark bottom edge
                    if (cell.flagged) drawFlag(ren, x, y);
                }
            }

            // Show wrongly placed flags after a loss
            if (g.state == LOST)
                for (int r = 0; r < g.rows; r++)
                    for (int c = 0; c < g.cols; c++) {
                        Cell &cell = g.at(c, r);
                        if (cell.flagged && !cell.mine) {
                            int x = c * CS, y = r * CS;
                            fillRect(ren, x + 4, y + 14, CS - 8, 4, {220, 40, 40, 255});
                        }
                    }

                    SDL_RenderPresent(ren);
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
