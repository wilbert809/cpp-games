// Chess in C++ with SDL2
// Rules included: all piece moves, check, checkmate, stalemate, castling,
// en passant, and pawn promotion (a pawn that reaches the end becomes a queen).
//
// Controls:
//   Click a piece to select it, then click a highlighted square to move.
//   1 = play vs computer (easy)   2 = play vs computer (hard)   3 = two players
//   U = undo   R = restart   Esc = quit
//
// Build: g++ chess.cpp -o chess $(sdl2-config --cflags --libs)

#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

const int CS = 80; // size of one board square in pixels

enum { EMPTY = 0, PAWN = 1, KNIGHT = 2, BISHOP = 3, ROOK = 4, QUEEN = 5, KING = 6 };

// ---------------------------------------------------------------------------
//  Board and moves
// ---------------------------------------------------------------------------
// Squares are numbered 0..63: row * 8 + column. Row 0 is the TOP of the screen
// (black's back rank), row 7 is the bottom (white's back rank).
// White pieces are positive numbers, black pieces are negative.

struct Move { int from, to, promo; };

struct Position {
    int8_t b[64];
    bool whiteToMove;
    bool castle[4]; // white king side, white queen side, black king side, black queen side
    int ep;         // square a pawn can capture "en passant" onto, or -1
};

static const int KN[8][2]   = {{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
static const int DIRS[8][2] = {{-1,0},{1,0},{0,-1},{0,1},{-1,-1},{-1,1},{1,-1},{1,1}};

static bool inside(int r, int c) { return r >= 0 && r < 8 && c >= 0 && c < 8; }

static void setup(Position &p) {
    const int back[8] = {ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK};
    std::memset(p.b, 0, sizeof p.b);
    for (int c = 0; c < 8; c++) {
        p.b[0 * 8 + c] = -back[c];
        p.b[1 * 8 + c] = -PAWN;
        p.b[6 * 8 + c] = PAWN;
        p.b[7 * 8 + c] = back[c];
    }
    p.whiteToMove = true;
    for (int i = 0; i < 4; i++) p.castle[i] = true;
    p.ep = -1;
}

// Is square `sq` attacked by any piece of the given color?
static bool attacked(const Position &p, int sq, bool byWhite) {
    int r = sq / 8, c = sq % 8;
    int sign = byWhite ? 1 : -1;

    int pr = byWhite ? r + 1 : r - 1; // where an attacking pawn would stand
    for (int dc = -1; dc <= 1; dc += 2)
        if (inside(pr, c + dc) && p.b[pr * 8 + c + dc] == sign * PAWN) return true;

        for (auto &k : KN)
            if (inside(r + k[0], c + k[1]) && p.b[(r + k[0]) * 8 + c + k[1]] == sign * KNIGHT) return true;

            for (int dr = -1; dr <= 1; dr++)
                for (int dc = -1; dc <= 1; dc++)
                    if ((dr || dc) && inside(r + dr, c + dc) && p.b[(r + dr) * 8 + c + dc] == sign * KING) return true;

                    for (int d = 0; d < 8; d++) {
                        int rr = r + DIRS[d][0], cc = c + DIRS[d][1];
                        while (inside(rr, cc)) {
                            int piece = p.b[rr * 8 + cc];
                            if (piece != 0) {
                                bool straight = d < 4;
                                if (piece == sign * QUEEN || (straight && piece == sign * ROOK) ||
                                    (!straight && piece == sign * BISHOP))
                                    return true;
                                break;
                            }
                            rr += DIRS[d][0];
                            cc += DIRS[d][1];
                        }
                    }
                    return false;
}

static int findKing(const Position &p, bool white) {
    for (int i = 0; i < 64; i++)
        if (p.b[i] == (white ? KING : -KING)) return i;
        return -1;
}

static bool inCheck(const Position &p, bool white) {
    int k = findKing(p, white);
    return k >= 0 && attacked(p, k, !white);
}

// All moves that follow the piece movement rules (but might leave our own king in check)
static void genPseudo(const Position &p, std::vector<Move> &out) {
    bool white = p.whiteToMove;
    int sign = white ? 1 : -1;

    for (int sq = 0; sq < 64; sq++) {
        int piece = p.b[sq];
        if (piece * sign <= 0) continue;
        int type = std::abs(piece), r = sq / 8, c = sq % 8;

        if (type == PAWN) {
            int dir = white ? -1 : 1, startRow = white ? 6 : 1, promoRow = white ? 0 : 7;
            auto add = [&](int to) { out.push_back({sq, to, (to / 8 == promoRow) ? QUEEN : 0}); };
            int r1 = r + dir;
            if (r1 >= 0 && r1 < 8) {
                if (p.b[r1 * 8 + c] == 0) {
                    add(r1 * 8 + c);
                    int r2 = r + 2 * dir;
                    if (r == startRow && p.b[r2 * 8 + c] == 0) add(r2 * 8 + c);
                }
                for (int dc = -1; dc <= 1; dc += 2) {
                    int cc = c + dc;
                    if (cc < 0 || cc >= 8) continue;
                    int to = r1 * 8 + cc;
                    if (p.b[to] * sign < 0 || to == p.ep) add(to);
                }
            }
        } else if (type == KNIGHT) {
            for (auto &k : KN) {
                int rr = r + k[0], cc = c + k[1];
                if (inside(rr, cc) && p.b[rr * 8 + cc] * sign <= 0) out.push_back({sq, rr * 8 + cc, 0});
            }
        } else if (type == KING) {
            for (int dr = -1; dr <= 1; dr++)
                for (int dc = -1; dc <= 1; dc++) {
                    int rr = r + dr, cc = c + dc;
                    if ((dr || dc) && inside(rr, cc) && p.b[rr * 8 + cc] * sign <= 0)
                        out.push_back({sq, rr * 8 + cc, 0});
                }
                // Castling
                int home = white ? 7 : 0, base = home * 8;
            if (r == home && c == 4 && !attacked(p, sq, !white)) {
                int ks = white ? 0 : 2, qs = ks + 1;
                if (p.castle[ks] && p.b[base + 5] == 0 && p.b[base + 6] == 0 &&
                    p.b[base + 7] == sign * ROOK &&
                    !attacked(p, base + 5, !white) && !attacked(p, base + 6, !white))
                    out.push_back({sq, base + 6, 0});
                if (p.castle[qs] && p.b[base + 3] == 0 && p.b[base + 2] == 0 && p.b[base + 1] == 0 &&
                    p.b[base + 0] == sign * ROOK &&
                    !attacked(p, base + 3, !white) && !attacked(p, base + 2, !white))
                    out.push_back({sq, base + 2, 0});
            }
        } else { // bishop, rook, queen: slide in straight lines
            int startDir = (type == BISHOP) ? 4 : 0;
            int endDir = (type == ROOK) ? 4 : 8;
            for (int d = startDir; d < endDir; d++) {
                int rr = r + DIRS[d][0], cc = c + DIRS[d][1];
                while (inside(rr, cc)) {
                    int target = p.b[rr * 8 + cc];
                    if (target * sign > 0) break; // blocked by our own piece
                    out.push_back({sq, rr * 8 + cc, 0});
                    if (target != 0) break;       // captured something, stop
                    rr += DIRS[d][0];
                    cc += DIRS[d][1];
                }
            }
        }
    }
}

static void makeMove(Position &p, const Move &m) {
    int piece = p.b[m.from];
    int type = std::abs(piece);
    bool white = piece > 0;
    int fr = m.from / 8, fc = m.from % 8, tr = m.to / 8, tc = m.to % 8;

    // En passant: a pawn moving diagonally onto an empty square captures the pawn beside it
    if (type == PAWN && fc != tc && p.b[m.to] == 0) p.b[fr * 8 + tc] = 0;

    // Castling: the rook jumps over the king
    if (type == KING && std::abs(tc - fc) == 2) {
        if (tc > fc) { p.b[tr * 8 + 5] = p.b[tr * 8 + 7]; p.b[tr * 8 + 7] = 0; }
        else         { p.b[tr * 8 + 3] = p.b[tr * 8 + 0]; p.b[tr * 8 + 0] = 0; }
    }

    p.b[m.to] = piece;
    p.b[m.from] = 0;
    if (m.promo) p.b[m.to] = white ? m.promo : -m.promo;

    p.ep = -1;
    if (type == PAWN && std::abs(tr - fr) == 2) p.ep = ((fr + tr) / 2) * 8 + fc;

    if (type == KING) {
        if (white) p.castle[0] = p.castle[1] = false;
        else       p.castle[2] = p.castle[3] = false;
    }
    auto touch = [&](int s) { // moving from or capturing on a rook's corner removes castling
        if (s == 63) p.castle[0] = false;
        if (s == 56) p.castle[1] = false;
        if (s == 7)  p.castle[2] = false;
        if (s == 0)  p.castle[3] = false;
    };
        touch(m.from);
        touch(m.to);

        p.whiteToMove = !p.whiteToMove;
}

static std::vector<Move> legalMoves(const Position &p) {
    std::vector<Move> pseudo, legal;
    genPseudo(p, pseudo);
    for (auto &m : pseudo) {
        Position q = p;
        makeMove(q, m);
        if (!inCheck(q, p.whiteToMove)) legal.push_back(m); // can't leave our own king in check
    }
    return legal;
}

// ---------------------------------------------------------------------------
//  Computer player (looks a few moves ahead and picks the best one)
// ---------------------------------------------------------------------------
static int evaluate(const Position &p) { // positive = good for white
    static const int VAL[7] = {0, 100, 320, 330, 500, 900, 0};
    int score = 0;
    for (int sq = 0; sq < 64; sq++) {
        int piece = p.b[sq];
        if (!piece) continue;
        int type = std::abs(piece), r = sq / 8, c = sq % 8;
        int sign = piece > 0 ? 1 : -1;
        int v = VAL[type];
        int cen = (3 - std::min(std::abs(r - 3), std::abs(r - 4))) +
        (3 - std::min(std::abs(c - 3), std::abs(c - 4))); // 0 (edge) .. 6 (center)
        if (type == KNIGHT || type == BISHOP) v += cen * 6;
        else if (type == PAWN) v += (sign > 0 ? (6 - r) : (r - 1)) * 6 + ((c >= 2 && c <= 5) ? cen : 0);
        else if (type == QUEEN) v += cen;
        else if (type == KING) v -= cen * 4;
        score += sign * v;
    }
    return score;
}

static void orderMoves(const Position &p, std::vector<Move> &moves) {
    static const int VAL[7] = {0, 1, 3, 3, 5, 9, 10};
    auto key = [&](const Move &m) {
        int s = VAL[std::abs(p.b[m.to])] * 10 - VAL[std::abs(p.b[m.from])];
        if (m.promo) s += 80;
        return s;
    };
    std::sort(moves.begin(), moves.end(), [&](const Move &a, const Move &b) { return key(a) > key(b); });
}

static int negamax(const Position &p, int depth, int alpha, int beta) {
    if (depth == 0) return (p.whiteToMove ? 1 : -1) * evaluate(p);
    std::vector<Move> moves = legalMoves(p);
    if (moves.empty()) return inCheck(p, p.whiteToMove) ? -100000 - depth : 0; // mated, or stalemate
    orderMoves(p, moves);
    int best = -1000000;
    for (auto &m : moves) {
        Position q = p;
        makeMove(q, m);
        int s = -negamax(q, depth - 1, -beta, -alpha);
        best = std::max(best, s);
        alpha = std::max(alpha, s);
        if (alpha >= beta) break;
    }
    return best;
}

static Move pickMove(const Position &p, int depth) {
    std::vector<Move> moves = legalMoves(p);
    orderMoves(p, moves);
    int best = -1000000;
    std::vector<Move> bests;
    for (auto &m : moves) {
        Position q = p;
        makeMove(q, m);
        int s = -negamax(q, depth - 1, -1000000, 1000000);
        if (s > best) { best = s; bests.clear(); }
        if (s == best) bests.push_back(m);
    }
    return bests[rand() % bests.size()]; // if several moves are equally good, pick any
}

// ---------------------------------------------------------------------------
//  Game state
// ---------------------------------------------------------------------------
struct Snap { Position p; int lastFrom, lastTo; };

struct Game {
    Position pos;
    std::vector<Snap> hist;
    std::vector<Move> moves; // legal moves right now
    int sel = -1, lastFrom = -1, lastTo = -1;
    int mode = 0; // 0 = vs easy CPU, 1 = vs hard CPU, 2 = two players
    bool over = false;

    void refresh() { moves = legalMoves(pos); over = moves.empty(); }

    void newGame() {
        setup(pos);
        hist.clear();
        sel = lastFrom = lastTo = -1;
        refresh();
    }

    void play(Move m) {
        hist.push_back({pos, lastFrom, lastTo});
        makeMove(pos, m);
        lastFrom = m.from;
        lastTo = m.to;
        sel = -1;
        refresh();
    }

    void popOne() {
        Snap s = hist.back();
        hist.pop_back();
        pos = s.p;
        lastFrom = s.lastFrom;
        lastTo = s.lastTo;
    }

    void undo() {
        if (hist.empty()) return;
        popOne();
        if (mode != 2 && !pos.whiteToMove && !hist.empty()) popOne(); // also take back the computer's reply
        sel = -1;
        refresh();
    }
};

// ---------------------------------------------------------------------------
//  Drawing
// ---------------------------------------------------------------------------
// 5x7 pixel letters used to label the pieces (each row is 5 bits)
static const int LETTERS[7][7] = {
    {0, 0, 0, 0, 0, 0, 0},
    {30, 17, 17, 30, 16, 16, 16}, // P
    {17, 25, 25, 21, 19, 19, 17}, // N
    {30, 17, 17, 30, 17, 17, 30}, // B
    {30, 17, 17, 30, 20, 18, 17}, // R
    {14, 17, 17, 17, 21, 18, 13}, // Q
    {17, 18, 20, 24, 20, 18, 17}, // K
};

static void fillRect(SDL_Renderer *ren, int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderFillRect(ren, &rc);
}

static void fillCircle(SDL_Renderer *ren, int cx, int cy, int rad, SDL_Color c) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    for (int dy = -rad; dy <= rad; dy++) {
        int dx = (int)std::sqrt((double)(rad * rad - dy * dy));
        SDL_RenderDrawLine(ren, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

static void drawPiece(SDL_Renderer *ren, int piece, int x, int y) {
    bool white = piece > 0;
    int type = std::abs(piece);
    SDL_Color ring = white ? SDL_Color{40, 40, 40, 255} : SDL_Color{235, 235, 235, 255};
    SDL_Color body = white ? SDL_Color{250, 245, 230, 255} : SDL_Color{50, 50, 60, 255};
    SDL_Color ink  = white ? SDL_Color{30, 30, 30, 255} : SDL_Color{245, 245, 245, 255};
    int cx = x + CS / 2, cy = y + CS / 2;
    fillCircle(ren, cx, cy, 34, ring);
    fillCircle(ren, cx, cy, 31, body);
    const int S = 5; // each letter pixel is 5x5 screen pixels
    int ox = cx - (5 * S) / 2, oy = cy - (7 * S) / 2;
    for (int row = 0; row < 7; row++)
        for (int col = 0; col < 5; col++)
            if (LETTERS[type][row] & (16 >> col)) fillRect(ren, ox + col * S, oy + row * S, S, S, ink);
}

// ---------------------------------------------------------------------------
int main(int, char **) {
    srand((unsigned)time(nullptr));
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow("Chess", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       8 * CS, 8 * CS, SDL_WINDOW_SHOWN);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!win || !ren) {
        std::printf("Window/renderer failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    Game g;
    g.newGame();
    char lastTitle[200] = "";
    bool running = true;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;

            if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE: running = false; break;
                    case SDLK_r: g.newGame(); break;
                    case SDLK_u: g.undo(); break;
                    case SDLK_1: g.mode = 0; g.newGame(); break;
                    case SDLK_2: g.mode = 1; g.newGame(); break;
                    case SDLK_3: g.mode = 2; g.newGame(); break;
                }
            }

            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && !g.over) {
                bool humanTurn = (g.mode == 2) || g.pos.whiteToMove;
                int c = e.button.x / CS, r = e.button.y / CS;
                if (humanTurn && inside(r, c)) {
                    int sq = r * 8 + c;
                    bool moved = false;
                    if (g.sel != -1) {
                        for (auto &m : g.moves)
                            if (m.from == g.sel && m.to == sq) {
                                g.play(m);
                                moved = true;
                                break;
                            }
                    }
                    if (!moved) {
                        int sign = g.pos.whiteToMove ? 1 : -1;
                        g.sel = (g.pos.b[sq] * sign > 0) ? sq : -1;
                    }
                }
            }
        }

        // ---- Title bar: whose turn / check / game over ----
        const char *modeName = g.mode == 0 ? "vs Easy CPU" : g.mode == 1 ? "vs Hard CPU" : "Two players";
        bool check = inCheck(g.pos, g.pos.whiteToMove);
        const char *status;
        if (g.over) status = check ? (g.pos.whiteToMove ? "CHECKMATE - Black wins!" : "CHECKMATE - White wins!")
            : "STALEMATE - it's a draw";
        else if (check) status = g.pos.whiteToMove ? "White to move - CHECK!" : "Black to move - CHECK!";
        else status = g.pos.whiteToMove ? "White to move" : "Black to move";
        char title[200];
        std::snprintf(title, sizeof title, "Chess (%s) | %s | 1/2/3 = mode, U = undo, R = restart", modeName, status);
        if (std::strcmp(title, lastTitle) != 0) {
            SDL_SetWindowTitle(win, title);
            std::strcpy(lastTitle, title);
        }

        // ---- Draw the board ----
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++) {
                bool light = (r + c) % 2 == 0;
                fillRect(ren, c * CS, r * CS, CS, CS,
                         light ? SDL_Color{240, 217, 181, 255} : SDL_Color{181, 136, 99, 255});
            }

            // Last move (yellow), selected piece (green), king in check (red)
            if (g.lastFrom >= 0) {
                fillRect(ren, (g.lastFrom % 8) * CS, (g.lastFrom / 8) * CS, CS, CS, {255, 230, 80, 110});
                fillRect(ren, (g.lastTo % 8) * CS, (g.lastTo / 8) * CS, CS, CS, {255, 230, 80, 110});
            }
            if (g.sel >= 0) fillRect(ren, (g.sel % 8) * CS, (g.sel / 8) * CS, CS, CS, {90, 200, 90, 150});
            if (check) {
                int k = findKing(g.pos, g.pos.whiteToMove);
                if (k >= 0) fillRect(ren, (k % 8) * CS, (k / 8) * CS, CS, CS, {230, 50, 50, 170});
            }

            // Pieces
            for (int sq = 0; sq < 64; sq++)
                if (g.pos.b[sq]) drawPiece(ren, g.pos.b[sq], (sq % 8) * CS, (sq / 8) * CS);

                // Where the selected piece can go: dot for a quiet move, red square for a capture
                if (g.sel >= 0)
                    for (auto &m : g.moves)
                        if (m.from == g.sel) {
                            int x = (m.to % 8) * CS, y = (m.to / 8) * CS;
                            bool capture = g.pos.b[m.to] != 0 ||
                            (std::abs(g.pos.b[m.from]) == PAWN && (m.from % 8) != (m.to % 8));
                            if (capture) fillRect(ren, x, y, CS, CS, {230, 50, 50, 110});
                            else fillCircle(ren, x + CS / 2, y + CS / 2, 11, {30, 30, 30, 110});
                        }

                        SDL_RenderPresent(ren);

                    // ---- Computer's turn (we drew the board first, so your move is visible while it thinks) ----
                    if (g.mode != 2 && !g.over && !g.pos.whiteToMove) {
                        int depth = (g.mode == 0) ? 2 : 4;
                        g.play(pickMove(g.pos, depth));
                    }
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
