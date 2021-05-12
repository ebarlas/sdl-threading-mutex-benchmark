#include <functional>
#include <vector>
#include <numeric>
#include "SDL.h"

struct Runner {
    SDL_Window *window;
    SDL_Renderer *renderer;
    int width;
    int height;
    int duration;
    bool quit;

    Runner(int width, int height, int duration) : width(width), height(height), duration(duration), quit(false) {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            SDL_Log("SDL could not initialize. SDL Error: %s", SDL_GetError());
            std::terminate();
        }

        window = SDL_CreateWindow(
                "Benchmark",
                SDL_WINDOWPOS_UNDEFINED,
                SDL_WINDOWPOS_UNDEFINED,
                width,
                height,
                SDL_WINDOW_SHOWN);
        if (window == nullptr) {
            SDL_Log("Window could not be created. SDL Error: %s", SDL_GetError());
            std::terminate();
        }

        renderer = SDL_CreateRenderer(
                window,
                -1,
                0);
        if (renderer == nullptr) {
            SDL_Log("Renderer could not be created. SDL Error: %s", SDL_GetError());
            std::terminate();
        }
    }

    ~Runner() {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

    int run(std::function<void(SDL_Renderer *)> fn) {
        Uint32 deadline = SDL_GetTicks() + duration;
        int count = 0;
        while (!quit && SDL_GetTicks() < deadline) {
            SDL_Event e;
            while (SDL_PollEvent(&e) != 0) {
                if (e.type == SDL_QUIT) {
                    quit = true;
                }
            }
            fn(renderer);
            SDL_RenderPresent(renderer);
            count++;
        }
        return count / (duration / 1000);
    }
};

int runFunction(void *data) {
    auto fn = (std::function<void()> *) data;
    (*fn)();
    return 0;
}

void fillRects(SDL_Renderer *ren, SDL_Rect *rects, int numRects) {
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_RenderFillRects(ren, rects, numRects);
}

SDL_Rect randomRect(SDL_Point size, int sideLen) {
    return {std::rand() % (size.x - sideLen), std::rand() % (size.y - sideLen), sideLen, sideLen};
}

void moveRect(SDL_Rect &rect, SDL_Point &size) {
    rect.x = (rect.x + 1) % size.x;
    rect.y = (rect.y + 1) % size.y;
}

int runSingleThreadNoMutex(Runner &runner, int numRects, int updatePeriod, int sideLen) {
    SDL_Point size{runner.width, runner.height};

    auto rects = new SDL_Rect[numRects];
    for (int i = 0; i < numRects; i++) {
        rects[i] = randomRect(size, sideLen);
    }

    Uint32 updateTime = 0;
    auto fn = [&size, rects, numRects, updatePeriod, &updateTime](SDL_Renderer *ren) {
        Uint32 now = SDL_GetTicks();
        if (now - updateTime >= updatePeriod) {
            for (int i = 0; i < numRects; i++) {
                moveRect(rects[i], size);
            }
            updateTime = now;
        }
        fillRects(ren, rects, numRects);
    };
    int fps = runner.run(fn);

    delete[]rects;

    return fps;
}

int runMultiThreadedMultiMutex(Runner &runner, int numRects, int updatePeriod, int sideLen) {
    SDL_Point size{runner.width, runner.height};

    auto updateRects = new SDL_Rect[numRects];
    auto mutexes = new void *[numRects];
    for (int i = 0; i < numRects; i++) {
        updateRects[i] = randomRect(size, sideLen);
        mutexes[i] = SDL_CreateMutex();
    }

    SDL_atomic_t quit;
    SDL_AtomicSet(&quit, 0);

    std::function<void()> updateFn = [&size, updateRects, mutexes, numRects, updatePeriod, &quit]() {
        while (!SDL_AtomicGet(&quit)) {
            for (int i = 0; i < numRects; i++) {
                auto mutex = static_cast<SDL_mutex *>(mutexes[i]);
                SDL_LockMutex(mutex);
                moveRect(updateRects[i], size);
                SDL_UnlockMutex(mutex);
            }
            SDL_Delay(updatePeriod);
        }
    };

    SDL_Thread *thread = SDL_CreateThread(runFunction, "update-thread", &updateFn);

    auto renderRects = new SDL_Rect[numRects];

    auto renderFn = [updateRects, renderRects, mutexes, numRects](SDL_Renderer *ren) {
        for (int i = 0; i < numRects; i++) {
            auto mutex = static_cast<SDL_mutex *>(mutexes[i]);
            SDL_LockMutex(mutex);
            renderRects[i] = updateRects[i];
            SDL_UnlockMutex(mutex);
        }
        fillRects(ren, renderRects, numRects);
    };
    int fps = runner.run(renderFn);

    int status;
    SDL_AtomicSet(&quit, 1);
    SDL_WaitThread(thread, &status);

    for (int i = 0; i < numRects; i++) {
        SDL_DestroyMutex(static_cast<SDL_mutex *>(mutexes[i]));
    }

    delete[]updateRects;
    delete[]mutexes;
    delete[]renderRects;

    return fps;
}

int runMultiThreadedSingleMutex(Runner &runner, int numRects, int updatePeriod, int sideLen) {
    SDL_Point size{runner.width, runner.height};

    auto updateRects = new SDL_Rect[numRects];
    auto mutex = SDL_CreateMutex();
    for (int i = 0; i < numRects; i++) {
        updateRects[i] = randomRect(size, sideLen);
    }

    SDL_atomic_t quit;
    SDL_AtomicSet(&quit, 0);

    std::function<void()> updateFn = [&size, updateRects, mutex, numRects, updatePeriod, &quit]() {
        while (!SDL_AtomicGet(&quit)) {
            SDL_LockMutex(mutex);
            for (int i = 0; i < numRects; i++) {
                moveRect(updateRects[i], size);
            }
            SDL_UnlockMutex(mutex);
            SDL_Delay(updatePeriod);
        }
    };

    SDL_Thread *thread = SDL_CreateThread(runFunction, "update-thread", &updateFn);

    auto renderRects = new SDL_Rect[numRects];

    auto renderFn = [updateRects, renderRects, mutex, numRects](SDL_Renderer *ren) {
        SDL_LockMutex(mutex);
        for (int i = 0; i < numRects; i++) {
            renderRects[i] = updateRects[i];
        }
        SDL_UnlockMutex(mutex);
        fillRects(ren, renderRects, numRects);
    };
    int fps = runner.run(renderFn);

    int status;
    SDL_AtomicSet(&quit, 1);
    SDL_WaitThread(thread, &status);

    SDL_DestroyMutex(mutex);

    delete[]updateRects;
    delete[]renderRects;

    return fps;
}

int avgMinExcluded(std::vector<int> &vals) {
    std::sort(vals.begin(), vals.end());
    return std::accumulate(vals.begin() + 1, vals.end(), 0) / (vals.size() - 1);
}

int main() {
    int width = 640;
    int height = 640;
    int duration = 5'000;
    int updatePeriod = 5;
    int sideLen = 3;
    int runs = 4;
    int numRectsArr[] = {100, 1'000, 5'000, 10'000, 20'000, 50'000};
    auto fns = {runSingleThreadNoMutex, runMultiThreadedMultiMutex, runMultiThreadedSingleMutex};
    auto labels = {"single-threaded no-mutex", "multi-threaded mutex-per-particle", "multi-threaded single-mutex"};

    Runner runner(width, height, duration);

    auto label = std::begin(labels);
    for (auto fn : fns) {
        SDL_Log("Running %s test case...", *label);
        for (int numRects : numRectsArr) {
            std::vector<int> fpsvec;
            for (int i = 0; i < runs; i++) {
                int fps = fn(runner, numRects, updatePeriod, sideLen);
                fpsvec.push_back(fps);
                SDL_Log("i=%d, numRects=%d, fps=%d", i, numRects, fps);
            }
            SDL_Log("numRects=%d, avgFps=%d", numRects, avgMinExcluded(fpsvec));
        }
        label++;
    }

    return 0;
}
