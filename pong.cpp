// Pong in C++ with SDL2
// Controls:
//   Left paddle:  W / S  (or gamepad left stick / D-pad)
//   Right paddle: Up / Down (only in 2-player mode)
//   1 = play vs computer, 2 = two players, Esc = quit
//
// Build: g++ pong.cpp -o pong $(sdl2-config --cflags --libs)

#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

const int W = 800, H = 600;
const float PADDLE_W = 15, PADDLE_H = 100, BALL_SIZE = 15;
const float PADDLE_SPEED = 450.0f;
const float BALL_START_SPEED = 350.0f;
const float BALL_MAX_SPEED = 900.0f;

struct Ball { float x, y, vx, vy; };

static void resetBall(Ball &b, int dir) {
    b.x = W / 2.0f - BALL_SIZE / 2;
    b.y = H / 2.0f - BALL_SIZE / 2;
    float angle = ((rand() % 61) - 30) * 3.14159f / 180.0f; // -30..30 degrees
    b.vx = dir * BALL_START_SPEED * std::cos(angle);
    b.vy = BALL_START_SPEED * std::sin(angle);
}

static bool overlap(float ax, float ay, float aw, float ah,
                    float bx, float by, float bw, float bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
                    }

                    int main(int, char **) {
                        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
                            std::printf("SDL_Init failed: %s\n", SDL_GetError());
                            return 1;
                        }

                        SDL_Window *win = SDL_CreateWindow("Pong", SDL_WINDOWPOS_CENTERED,
                                                           SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_SHOWN);
                        SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
                                                               SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
                        if (!win || !ren) {
                            std::printf("Window/renderer failed: %s\n", SDL_GetError());
                            return 1;
                        }

                        // Open the first gamepad found (the Steam Deck's built-in controls count)
                        SDL_GameController *pad = nullptr;
                        for (int i = 0; i < SDL_NumJoysticks(); i++) {
                            if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }
                        }

                        float leftY = H / 2.0f - PADDLE_H / 2;
                        float rightY = leftY;
                        int leftScore = 0, rightScore = 0;
                        bool twoPlayer = false;
                        Ball ball;
                        resetBall(ball, (rand() % 2) ? 1 : -1);

                        auto updateTitle = [&]() {
                            char t[128];
                            std::snprintf(t, sizeof t, "Pong  |  %d : %d  |  %s  (1 = vs CPU, 2 = two players)",
                                          leftScore, rightScore, twoPlayer ? "2 players" : "vs CPU");
                            SDL_SetWindowTitle(win, t);
                        };
                        updateTitle();

                        bool running = true;
                        Uint32 last = SDL_GetTicks();

                        while (running) {
                            SDL_Event e;
                            while (SDL_PollEvent(&e)) {
                                if (e.type == SDL_QUIT) running = false;
                                if (e.type == SDL_KEYDOWN) {
                                    if (e.key.keysym.sym == SDLK_ESCAPE) running = false;
                                    if (e.key.keysym.sym == SDLK_1) { twoPlayer = false; updateTitle(); }
                                    if (e.key.keysym.sym == SDLK_2) { twoPlayer = true;  updateTitle(); }
                                }
                                if (e.type == SDL_CONTROLLERBUTTONDOWN &&
                                    e.cbutton.button == SDL_CONTROLLER_BUTTON_START) running = false;
                            }

                            Uint32 now = SDL_GetTicks();
                            float dt = std::min((now - last) / 1000.0f, 0.05f);
                            last = now;

                            // ---- Input ----
                            const Uint8 *keys = SDL_GetKeyboardState(nullptr);
                            float leftDir = 0, rightDir = 0;
                            if (keys[SDL_SCANCODE_W]) leftDir -= 1;
                            if (keys[SDL_SCANCODE_S]) leftDir += 1;

                            if (pad) {
                                float stick = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
                                if (std::fabs(stick) > 0.2f) leftDir += stick;
                                if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) leftDir -= 1;
                                if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) leftDir += 1;
                            }
                            leftDir = std::max(-1.0f, std::min(1.0f, leftDir));

                            if (twoPlayer) {
                                if (keys[SDL_SCANCODE_UP]) rightDir -= 1;
                                if (keys[SDL_SCANCODE_DOWN]) rightDir += 1;
                            } else {
                                // Simple computer opponent: follow the ball, with a small dead zone
                                float center = rightY + PADDLE_H / 2;
                                float ballCenter = ball.y + BALL_SIZE / 2;
                                if (ballCenter < center - 15) rightDir = -0.85f;
                                else if (ballCenter > center + 15) rightDir = 0.85f;
                            }

                            leftY += leftDir * PADDLE_SPEED * dt;
                            rightY += rightDir * PADDLE_SPEED * dt;
                            leftY = std::max(0.0f, std::min(H - PADDLE_H, leftY));
                            rightY = std::max(0.0f, std::min(H - PADDLE_H, rightY));

                            // ---- Ball ----
                            ball.x += ball.vx * dt;
                            ball.y += ball.vy * dt;

                            // Top / bottom walls
                            if (ball.y <= 0) { ball.y = 0; ball.vy = std::fabs(ball.vy); }
                            if (ball.y + BALL_SIZE >= H) { ball.y = H - BALL_SIZE; ball.vy = -std::fabs(ball.vy); }

                            const float leftX = 30, rightX = W - 30 - PADDLE_W;

                            auto bounce = [&](float paddleY, int dirAfter) {
                                float rel = ((ball.y + BALL_SIZE / 2) - (paddleY + PADDLE_H / 2)) / (PADDLE_H / 2);
                                rel = std::max(-1.0f, std::min(1.0f, rel));
                                float speed = std::min(std::sqrt(ball.vx * ball.vx + ball.vy * ball.vy) * 1.06f,
                                                       BALL_MAX_SPEED);
                                float angle = rel * 60.0f * 3.14159f / 180.0f; // up to 60 degrees
                                ball.vx = dirAfter * speed * std::cos(angle);
                                ball.vy = speed * std::sin(angle);
                            };

                            if (ball.vx < 0 && overlap(ball.x, ball.y, BALL_SIZE, BALL_SIZE, leftX, leftY, PADDLE_W, PADDLE_H)) {
                                ball.x = leftX + PADDLE_W;
                                bounce(leftY, 1);
                            }
                            if (ball.vx > 0 && overlap(ball.x, ball.y, BALL_SIZE, BALL_SIZE, rightX, rightY, PADDLE_W, PADDLE_H)) {
                                ball.x = rightX - BALL_SIZE;
                                bounce(rightY, -1);
                            }

                            // Scoring
                            if (ball.x + BALL_SIZE < 0) { rightScore++; resetBall(ball, 1);  updateTitle(); SDL_Delay(400); last = SDL_GetTicks(); }
                            if (ball.x > W)             { leftScore++;  resetBall(ball, -1); updateTitle(); SDL_Delay(400); last = SDL_GetTicks(); }

                            // ---- Draw ----
                            SDL_SetRenderDrawColor(ren, 15, 15, 25, 255);
                            SDL_RenderClear(ren);

                            SDL_SetRenderDrawColor(ren, 70, 70, 90, 255);
                            for (int y = 0; y < H; y += 30) {
                                SDL_Rect dash = {W / 2 - 2, y, 4, 15};
                                SDL_RenderFillRect(ren, &dash);
                            }

                            SDL_SetRenderDrawColor(ren, 235, 235, 235, 255);
                            SDL_Rect lp = {(int)leftX, (int)leftY, (int)PADDLE_W, (int)PADDLE_H};
                            SDL_Rect rp = {(int)rightX, (int)rightY, (int)PADDLE_W, (int)PADDLE_H};
                            SDL_Rect bl = {(int)ball.x, (int)ball.y, (int)BALL_SIZE, (int)BALL_SIZE};
                            SDL_RenderFillRect(ren, &lp);
                            SDL_RenderFillRect(ren, &rp);
                            SDL_RenderFillRect(ren, &bl);

                            SDL_RenderPresent(ren);
                        }

                        if (pad) SDL_GameControllerClose(pad);
                        SDL_DestroyRenderer(ren);
                        SDL_DestroyWindow(win);
                        SDL_Quit();
                        return 0;
                    }
