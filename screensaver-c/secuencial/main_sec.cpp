// main.cpp — Secuencial con overlay de FPS (tecla F) + barra inferior de FPS
// Compilar (MSYS2/MinGW64):
//   g++ -O2 -std=c++17 -Wall -Wextra -Wshadow main.cpp -o screensaver $(pkg-config --cflags --libs sdl2 SDL2_ttf)

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>

// ---------------- Utilidades ----------------
static float frand(float a, float b) { return a + (b - a) * (float(rand()) / float(RAND_MAX)); }
static float clampf(float x, float a, float b) { return std::max(a, std::min(b, x)); }
static bool  startsWith(const std::string& s, const std::string& pre){ return s.rfind(pre,0)==0; }
static float toFloat(const std::string& s, float def){ try{ return std::stof(s);}catch(...){return def;} }
static int   toInt  (const std::string& s, int def){ try{ return std::stoi(s);}catch(...){return def;} }

// Círculo relleno sin libs extra (SDL2)
static void drawFilledCircle(SDL_Renderer* r, int cx, int cy, int radius) {
    for (int y = -radius; y <= radius; ++y) {
        int inside = radius*radius - y*y;
        if (inside < 0) continue;
        int dx = (int)std::floor(std::sqrt((double)inside));
        SDL_RenderDrawLine(r, cx - dx, cy + y, cx + dx, cy + y);
    }
}

// ---------------- Física ----------------
struct Body {
    float x=0, y=0;
    float vx=0, vy=0;
    float radius=5;
    float mass=1;
    SDL_Color color{255,255,255,255};
    bool is_main=false;       // principales (verde, rojo)
    float eject_cooldown=0.f; // cuenta regresiva tras “salir disparado”
};

struct SimParams {
    int width=960, height=540;
    int N=10000;

    // Gravedad global (con signo por principal)
    float G=30.5f;

    // Principales (A=verde, B=rojo)
    float mainRadiusA=14.f, mainRadiusB=14.f;
    float mainMassA=50000.f, mainMassB=1000000.f;
    float mainInitSpeed=100.f; // rapidez inicial
    float mainDamping=1.0f;    // 1.0 = sin amortiguación
    float mainSignA=+1.f;      // +1 atrae, -1 repele (verde)
    float mainSignB=-1.f;      // +1 atrae, -1 repele (rojo)

    // Satélites
    float satRadius=4.f;
    float satMass=1.f;
    float maxInitSpeed=60.f;   // se usa *0.15 abajo
    float ejectSpeed=200.f;
    float ejectCooldownSec=0.60f;
    float postEjectGravityFactor=0.35f; // gravedad parcial al inicio del cooldown

    // Físicas comunes
    float wallRestitution=0.95f;
    float softening=8.0f;

    // Benchmark mode
    bool benchmark = false;
    int benchmarkFrames = 500;
};

struct SimState {
    Body mainA, mainB;
    Body mainA2, mainB2;
    std::vector<Body> sats;
};

// Colisión elástica 2D entre dos círculos (solo principales)
static void resolveElasticCollision(Body& a, Body& b) {
    float dx = b.x - a.x, dy = b.y - a.y;
    float dist2 = dx*dx + dy*dy;
    float minDist = a.radius + b.radius;

    if (dist2 <= 0.0001f) return;
    float dist = std::sqrt(dist2);
    if (dist >= minDist) return;

    // Separación mínima
    float overlap = 0.5f * (minDist - dist);
    float nx = dx / dist, ny = dy / dist;
    a.x -= nx * overlap; a.y -= ny * overlap;
    b.x += nx * overlap; b.y += ny * overlap;

    // Velocidad relativa sobre la normal
    float rvx = b.vx - a.vx, rvy = b.vy - a.vy;
    float relVel = rvx * nx + rvy * ny;
    if (relVel > 0) return;

    float m1 = a.mass, m2 = b.mass;
    float e = 1.0f; // elástica
    float j = -(1 + e) * relVel / (1.f/m1 + 1.f/m2);

    float jx = j * nx, jy = j * ny;
    a.vx -= jx / m1; a.vy -= jy / m1;
    b.vx += jx / m2; b.vy += jy / m2;
}

// Rebotar contra paredes
static void bounceWalls(Body& b, const SimParams& p) {
    if (b.x - b.radius < 0)        { b.x = b.radius;            b.vx = -b.vx * p.wallRestitution; }
    if (b.x + b.radius > p.width)  { b.x = p.width - b.radius;  b.vx = -b.vx * p.wallRestitution; }
    if (b.y - b.radius < 0)        { b.y = b.radius;            b.vy = -b.vy * p.wallRestitution; }
    if (b.y + b.radius > p.height) { b.y = p.height - b.radius; b.vy = -b.vy * p.wallRestitution; }
}

// Gravedad con signo y rampa tras eyección
static void applyGravityFromMains(Body& s, const Body& A, const Body& B, const Body& A2, const Body& B2, const SimParams& p, float dt) {
    auto gravOne = [&](const Body& M, float sign, float factor){
        float dx = M.x - s.x, dy = M.y - s.y;
        float r2 = dx*dx + dy*dy + p.softening*p.softening;
        float invr = 1.0f / std::sqrt(r2);
        float invr3 = invr*invr*invr;
        // sign=+1 atrae, sign=-1 repele
        float ax = sign * factor * p.G * M.mass * dx * invr3;
        float ay = sign * factor * p.G * M.mass * dy * invr3;
        s.vx += ax * dt; s.vy += ay * dt;
    };

    // Factor de gravedad durante cooldown: de ~35% → 100%
    float factor = 1.0f;
    if (s.eject_cooldown > 0.f && p.ejectCooldownSec > 0.f) {
        float t = 1.f - clampf(s.eject_cooldown / p.ejectCooldownSec, 0.f, 1.f); // 0→1
        factor = p.postEjectGravityFactor + (1.f - p.postEjectGravityFactor) * t;
    }

    gravOne(A, p.mainSignA, factor);
    gravOne(B, p.mainSignB, factor);
    gravOne(A2, p.mainSignA, factor);
    gravOne(B2, p.mainSignB, factor);
}

// ¿Satélite toca un principal? -> “sale disparado”
static void checkEject(Body& s, const Body& M, const SimParams& p) {
    float dx = s.x - M.x, dy = s.y - M.y;
    float dist2 = dx*dx + dy*dy;
    float minDist = s.radius + M.radius;
    if (dist2 <= minDist*minDist) {
        float d = std::sqrt(std::max(dist2, 1e-6f));
        float nx = dx / d, ny = dy / d;
        s.vx = nx * p.ejectSpeed;
        s.vy = ny * p.ejectSpeed;
        s.eject_cooldown = p.ejectCooldownSec;
        // sacarlo justo fuera
        float push = (minDist - d) + 0.5f;
        s.x += nx * push; s.y += ny * push;
    }
}

// ---------------- Inicialización ----------------
static void initSim(SimState& S, const SimParams& p) {
    S.sats.clear();
    // Principales
    S.mainA.is_main = true;  S.mainA.radius = p.mainRadiusA; S.mainA.mass = p.mainMassA;
    S.mainB.is_main = true;  S.mainB.radius = p.mainRadiusB; S.mainB.mass = p.mainMassB;

    S.mainA.x = p.width*0.33f; S.mainA.y = p.height*0.5f;
    S.mainB.x = p.width*0.66f; S.mainB.y = p.height*0.5f;

    S.mainA.vx = frand(-p.mainInitSpeed, p.mainInitSpeed);
    S.mainA.vy = frand(-p.mainInitSpeed, p.mainInitSpeed);
    S.mainB.vx = frand(-p.mainInitSpeed, p.mainInitSpeed);
    S.mainB.vy = frand(-p.mainInitSpeed, p.mainInitSpeed);

    S.mainA.color = SDL_Color{  0,255,  0,255}; // verde (atrae)
    S.mainB.color = SDL_Color{255, 64, 64,255}; // rojo (repele)

    S.mainA2 = S.mainA;
    S.mainB2 = S.mainB; 

    S.mainB2.x = p.width*0.33f; S.mainA2.y = p.height*0.25f;
    S.mainA2.x = p.width*0.66f; S.mainB2.y = p.height*0.75f;

    S.mainA2.color = SDL_Color{  0,255,  0,255};     // verde
    S.mainB2.color = SDL_Color{255, 64, 64,255};   // rojo

    // Satélites
    S.sats.resize(p.N);
    for (int i=0;i<p.N;++i){
        Body b;
        b.radius = p.satRadius; b.mass = p.satMass;
        float t = frand(0.25f, 0.75f);
        b.x = S.mainA.x * (1-t) + S.mainB.x * t + frand(-40,40);
        b.y = S.mainA.y + frand(-80,80);
        b.vx = frand(-p.maxInitSpeed, p.maxInitSpeed)*0.15f;
        b.vy = frand(-p.maxInitSpeed, p.maxInitSpeed)*0.15f;
        b.color = SDL_Color{
            Uint8(180+std::rand()%70),
            Uint8(180+std::rand()%70),
            Uint8(200+std::rand()%55),
            255
        };
        S.sats[i] = b;
    }
}

// ---------------- Texto (SDL_ttf + fallback) ----------------
static TTF_Font* gFont = nullptr;
static void drawBlocksText(SDL_Renderer* r, int x, int y, SDL_Color col, const std::string& s) {
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    int w = 6, h = 10, pad=2;
    SDL_Rect rect{ x, y, w, h };
    for (size_t i=0;i<s.size();++i) {
        rect.x = x + int(i)*(w+pad);
        SDL_RenderFillRect(r, &rect);
    }
}
static void drawText(SDL_Renderer* r, int x, int y, SDL_Color col, const std::string& s) {
    if (gFont) {
        SDL_Surface* surf = TTF_RenderUTF8_Blended(gFont, s.c_str(), col);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
            SDL_FreeSurface(surf);
            if (tex) {
                SDL_Rect dst{ x, y, 0, 0 };
                SDL_QueryTexture(tex, nullptr, nullptr, &dst.w, &dst.h);
                SDL_RenderCopy(r, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
                return;
            }
        }
    }
    drawBlocksText(r, x, y, col, s);
}

// ---------------- Barra inferior de FPS (abajo) ----------------
static void renderFPSBottomBar(SDL_Renderer* renderer, const std::vector<float>& fpsHist, int W, int H) {
    const int barH = 56; // alto de la barra
    SDL_Rect bar{0, H - barH, W, barH};

    // Fondo semitransparente
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_RenderFillRect(renderer, &bar);

    // Borde superior
    SDL_SetRenderDrawColor(renderer, 220, 220, 220, 220);
    SDL_RenderDrawLine(renderer, 0, H - barH, W, H - barH);

    int x = 12;
    int y = H - barH + 10;

    // Construimos la cadena "FPS: v1 v2 ... v10" (de viejo → nuevo)
    std::string s = "FPS: ";
    for (size_t i = 0; i < fpsHist.size(); ++i) {
        if (i) s += ' ';
        s += std::to_string((int)std::round(fpsHist[i]));
    }
    drawText(renderer, x, y, SDL_Color{200,200,255,255}, s);

    // Indicamos cuál es el más reciente
    if (!fpsHist.empty()) {
        int latest = (int)std::round(fpsHist.back());
        drawText(renderer, x, y + 22, SDL_Color{220,220,220,255}, std::string("Actual: ") + std::to_string(latest) + " FPS");
    }
}

// ---------------- Escena principal ----------------
static void renderSim(SDL_Renderer* r, const SimState& S, const SimParams& p, const std::vector<float>& fpsHist) {
    SDL_SetRenderDrawColor(r, 10, 14, 20, 255);
    SDL_RenderClear(r);


    // Satélites
    for (auto& b : S.sats) {
        SDL_SetRenderDrawColor(r, b.color.r, b.color.g, b.color.b, 255);
        drawFilledCircle(r, (int)std::lround(b.x), (int)std::lround(b.y), (int)b.radius);
    }
    // Principales
    SDL_SetRenderDrawColor(r, S.mainA.color.r, S.mainA.color.g, S.mainA.color.b, 255);
    drawFilledCircle(r, (int)std::lround(S.mainA.x), (int)std::lround(S.mainA.y), (int)S.mainA.radius);
    SDL_SetRenderDrawColor(r, S.mainB.color.r, S.mainB.color.g, S.mainB.color.b, 255);
    drawFilledCircle(r, (int)std::lround(S.mainB.x), (int)std::lround(S.mainB.y), (int)S.mainB.radius);
    SDL_SetRenderDrawColor(r, S.mainA2.color.r, S.mainA2.color.g, S.mainA2.color.b, 255);
    drawFilledCircle(r, (int)std::lround(S.mainA2.x), (int)std::lround(S.mainA2.y), (int)S.mainA2.radius);
    SDL_SetRenderDrawColor(r, S.mainB2.color.r, S.mainB2.color.g, S.mainB2.color.b, 255);
    drawFilledCircle(r, (int)std::lround(S.mainB2.x), (int)std::lround(S.mainB2.y), (int)S.mainB2.radius);

    // Barra inferior con la lista de FPS
    renderFPSBottomBar(r, fpsHist, p.width, p.height);
}

// ---------------- Overlay/Panel de FPS (tecla F) ----------------
static void renderFPSOverlay(SDL_Renderer* renderer, const std::vector<float>& fpsLog, int W, int H) {
    // Panel centrado con fondo semi-transparente
    int margin = 40;
    SDL_Rect panel{ margin, margin, W - 2*margin, H - 2*margin };

    // Fondo translúcido
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 18, 20, 26, 220);
    SDL_RenderFillRect(renderer, &panel);

    // Borde
    SDL_SetRenderDrawColor(renderer, 230, 230, 230, 255);
    SDL_RenderDrawRect(renderer, &panel);

    int x = panel.x + 20;
    int y = panel.y + 16;

    drawText(renderer, x, y, SDL_Color{160,210,255,255}, "PANEL DE FPS (F para cerrar)");
    y += 26;

    // Estadísticos rápidos (sobre lo último que tengamos, hasta 300)
    size_t take = std::min<size_t>(300, fpsLog.size());
    float avg = 0.f, mn = 1e9f, mx = 0.f;
    for (size_t i = fpsLog.size() - take; i < fpsLog.size(); ++i) {
        float v = fpsLog[i];
        avg += v; mn = std::min(mn, v); mx = std::max(mx, v);
    }
    if (take > 0) avg /= float(take); else { mn = 0; mx = 0; }
    drawText(renderer, x, y, SDL_Color{220,220,220,255},
        "muestras: " + std::to_string((int)take) +
        "   avg: " + std::to_string((int)std::round(avg)) +
        "   min: " + std::to_string((int)std::round(mn)) +
        "   max: " + std::to_string((int)std::round(mx)));
    y += 24;

    // Lista grande en columnas
    int usableH = panel.h - (y - panel.y) - 16;
    int rowH = 18;
    int rows = std::max(1, usableH / rowH);
    int cols = 4; // cuatro columnas
    int colW = (panel.w - 40) / cols;

    size_t maxItems = (size_t)(rows * cols);
    size_t count = std::min(maxItems, take);

    // mostramos los últimos 'count' valores
    size_t start = fpsLog.size() - count;
    for (int c = 0; c < cols; ++c) {
        for (int row = 0; row < rows; ++row) {
            size_t idxInBlock = (size_t)c * (size_t)rows + (size_t)row;
            size_t i = start + idxInBlock;
            if (i >= fpsLog.size()) break;
            int val = (int)std::round(fpsLog[i]);
            int cx = x + c * colW;
            int cy = y + row * rowH;
            drawText(renderer, cx, cy, SDL_Color{240,240,240,255}, std::to_string(val));
        }
    }
}

// ---------------- Menú (escoger) ----------------
enum class Mode { MENU, RUN, QUIT };

static const char* signLabel(float s){ return (s>=0.f) ? "ATRAE" : "REPELE"; }

static void drawMenu(SDL_Renderer* ren, const SimParams& P) {
    SDL_SetRenderDrawColor(ren, 18, 20, 26, 255);
    SDL_RenderClear(ren);

    drawText(ren, 40,  60, SDL_Color{120,200,255,255}, "SCREENSAVER SECUENCIAL - MENU");
    drawText(ren, 40, 110, SDL_Color{220,220,220,255}, "N (+/-50): " + std::to_string(P.N) + "  [N / Shift+N]");
    drawText(ren, 40, 140, SDL_Color{220,220,220,255}, "G (+/-0.5): " + std::to_string(P.G) + "  [G / Shift+G]");
    drawText(ren, 40, 170, SDL_Color{220,220,220,255}, "W/H (+/-32): " + std::to_string(P.width) + "x" + std::to_string(P.height));
    drawText(ren, 40, 210, SDL_Color{220,220,220,255}, "Mass A/B (+/-50): " + std::to_string((int)P.mainMassA) + " / " + std::to_string((int)P.mainMassB) + "  [A,B]");
    drawText(ren, 40, 240, SDL_Color{220,220,220,255}, "Radius A/B (+/-1): " + std::to_string((int)P.mainRadiusA) + " / " + std::to_string((int)P.mainRadiusB) + "  [R,T]");
    drawText(ren, 40, 270, SDL_Color{220,220,220,255}, "Main init speed (+/-10): " + std::to_string((int)P.mainInitSpeed) + "  [M]");
    drawText(ren, 40, 300, SDL_Color{220,220,220,255}, "Eject speed (+/-20): " + std::to_string((int)P.ejectSpeed) + "  [E]");
    drawText(ren, 40, 330, SDL_Color{220,220,220,255}, std::string("Verde: ") + signLabel(P.mainSignA) + "  [Z]    Rojo: " + signLabel(P.mainSignB) + "  [X]");
    drawText(ren, 40, 360, SDL_Color{200,200,200,255}, "ENTER: iniciar   |   ESC: salir");

    SDL_RenderPresent(ren);
}

static Mode runMenu(SDL_Window*& win, SDL_Renderer*& ren, SimParams& P) {
    while (true) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return Mode::QUIT;
            if (e.type == SDL_KEYDOWN) {
                bool shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
                switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE: return Mode::QUIT;
                    case SDLK_RETURN: return Mode::RUN;
                    case SDLK_n: P.N = std::max(0, P.N + (shift ? -50 : 50)); break;
                    case SDLK_g: P.G = std::max(0.f, P.G + (shift ? -0.5f : 0.5f)); break;
                    case SDLK_w: P.width  = std::max(640, P.width  + (shift ? -32 : 32)); break;
                    case SDLK_h: P.height = std::max(480, P.height + (shift ? -32 : 32)); break;
                    case SDLK_a: P.mainMassA = std::max(1.f, P.mainMassA + (shift ? -50.f : 50.f)); break;
                    case SDLK_b: P.mainMassB = std::max(1.f, P.mainMassB + (shift ? -50.f : 50.f)); break;
                    case SDLK_r: P.mainRadiusA = std::max(2.f, P.mainRadiusA + (shift ? -1.f : 1.f)); break;
                    case SDLK_t: P.mainRadiusB = std::max(2.f, P.mainRadiusB + (shift ? -1.f : 1.f)); break;
                    case SDLK_m: P.mainInitSpeed = std::max(0.f, P.mainInitSpeed + (shift ? -10.f : 10.f)); break;
                    case SDLK_e: P.ejectSpeed   = std::max(0.f, P.ejectSpeed   + (shift ? -20.f : 20.f)); break;
                    case SDLK_z: P.mainSignA = (P.mainSignA >= 0.f) ? -1.f : +1.f; break; // toggle
                    case SDLK_x: P.mainSignB = (P.mainSignB >= 0.f) ? -1.f : +1.f; break; // toggle
                    default: break;
                }
            }
        }
        // Redimensionar ventana si cambió
        int currentW, currentH;
        SDL_GetRendererOutputSize(ren, &currentW, &currentH);
        if (currentW != P.width || currentH != P.height) {
            SDL_SetWindowSize(win, P.width, P.height);
            SDL_RenderSetLogicalSize(ren, P.width, P.height);
        }
        drawMenu(ren, P);
        SDL_Delay(16);
    }
}

// ---------------- CLI args ----------------
static void parseArgs(int argc, char** argv, SimParams& P) {
    for (int i=1;i<argc;++i){
        std::string a(argv[i]);
        if (startsWith(a,"--N="))              P.N = std::max(0, toInt(a.substr(4), P.N));
        else if (startsWith(a,"--G="))         P.G = std::max(0.f, toFloat(a.substr(4), P.G));
        else if (startsWith(a,"--width="))     P.width  = std::max(640, toInt(a.substr(8), P.width));
        else if (startsWith(a,"--height="))    P.height = std::max(480, toInt(a.substr(9), P.height));
        else if (startsWith(a,"--massA="))     P.mainMassA = std::max(1.f, toFloat(a.substr(8), P.mainMassA));
        else if (startsWith(a,"--massB="))     P.mainMassB = std::max(1.f, toFloat(a.substr(8), P.mainMassB));
        else if (startsWith(a,"--radiusA="))   P.mainRadiusA = std::max(2.f, toFloat(a.substr(10), P.mainRadiusA));
        else if (startsWith(a,"--radiusB="))   P.mainRadiusB = std::max(2.f, toFloat(a.substr(10), P.mainRadiusB));
        else if (startsWith(a,"--mainInit="))  P.mainInitSpeed = std::max(0.f, toFloat(a.substr(11), P.mainInitSpeed));
        else if (startsWith(a,"--eject="))     P.ejectSpeed = std::max(0.f, toFloat(a.substr(8), P.ejectSpeed));
        else if (startsWith(a,"--satRadius=")) P.satRadius = std::max(1.f, toFloat(a.substr(12), P.satRadius));
        else if (startsWith(a,"--satMass="))   P.satMass   = std::max(0.1f, toFloat(a.substr(10), P.satMass));
        else if (startsWith(a,"--signA="))     P.mainSignA = clampf(toFloat(a.substr(8), P.mainSignA), -1.f, +1.f);
        else if (startsWith(a,"--signB="))     P.mainSignB = clampf(toFloat(a.substr(8), P.mainSignB), -1.f, +1.f);
        else if (a == "--benchmark")  P.benchmark = true;
        else if (startsWith(a,"--frames=")) P.benchmarkFrames = std::max(1, toInt(a.substr(9), P.benchmarkFrames));
        else std::cerr << "[warn] Arg no reconocido: " << a << "\n";
    }
}

// ---------------- Lógica de simulación ----------------
static void step(SimState& S, const SimParams& p, float dt) {
    // Principales: mover + paredes + amortiguación
    auto moveMain = [&](Body& M){
        M.x += M.vx * dt;
        M.y += M.vy * dt;
        bounceWalls(M, p);
        M.vx *= p.mainDamping;
        M.vy *= p.mainDamping;
    };

    moveMain(S.mainA);
    moveMain(S.mainB);
    moveMain(S.mainA2);
    moveMain(S.mainB2);

    resolveElasticCollision(S.mainA,  S.mainB);
    resolveElasticCollision(S.mainA,  S.mainA2);
    resolveElasticCollision(S.mainA,  S.mainB2);
    resolveElasticCollision(S.mainB,  S.mainA2);
    resolveElasticCollision(S.mainB,  S.mainB2);
    resolveElasticCollision(S.mainA2, S.mainB2);

    // Satélites
    for (auto& s : S.sats) {
        if (s.eject_cooldown > 0.f) s.eject_cooldown = std::max(0.f, s.eject_cooldown - dt);
        applyGravityFromMains(s, S.mainA, S.mainB, S.mainA2, S.mainB2, p, dt);
        s.x += s.vx * dt; s.y += s.vy * dt;
        bounceWalls(s, p);
        checkEject(s, S.mainA, p);
        checkEject(s, S.mainB, p);
        checkEject(s, S.mainA2, p);
        checkEject(s, S.mainB2, p);
    }
}

// ---------------- main ----------------
int main(int argc, char** argv) {
    std::srand(unsigned(std::time(nullptr)));
    SimParams P;
    parseArgs(argc, argv, P);

    // Ventana mínima 640x480
    P.width = std::max(P.width, 640);
    P.height = std::max(P.height, 480);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL_Init error: " << SDL_GetError() << "\n";
        return 1;
    }

    if (TTF_Init() != 0) {
        std::cerr << "TTF_Init: " << TTF_GetError() << "\n";
    } else {
        const int fontSize = 18;
        // Windows: intenta Consolas y luego Arial; si falla, hay fallback de "bloques"
        gFont = TTF_OpenFont("C:\\Windows\\Fonts\\consola.ttf", fontSize);
        if (!gFont) gFont = TTF_OpenFont("C:\\Windows\\Fonts\\arial.ttf", fontSize);
        if (!gFont) std::cerr << "TTF_OpenFont: " << TTF_GetError() << " (usando fallback de bloques)\n";
    }

    SDL_Window* win = SDL_CreateWindow(
        "Screensaver Secuencial (SDL2) — Verde atrae / Rojo repele",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        P.width, P.height, SDL_WINDOW_SHOWN);
    if (!win) { std::cerr << "SDL_CreateWindow error: " << SDL_GetError() << "\n"; return 1; }

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if (!ren) { std::cerr << "SDL_CreateRenderer error: " << SDL_GetError() << "\n"; return 1; }
    SDL_RenderSetLogicalSize(ren, P.width, P.height);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND); // necesario para los overlays

    // Benchmark mode
    if (P.benchmark) {
        SimState S;
        initSim(S, P);

        Uint64 t0 = SDL_GetPerformanceCounter();

        for (int frame = 0; frame < P.benchmarkFrames; frame++) {
            // dt fijo (~16 ms ≈ 60 FPS target)
            float dt = 0.016f;
            step(S, P, dt);

            renderSim(ren, S, P, {}); // {} = sin historial de FPS
            SDL_RenderPresent(ren);
        }

        Uint64 t1 = SDL_GetPerformanceCounter();
        double ms = (t1 - t0) * 1000.0 / SDL_GetPerformanceFrequency();
        std::cout << "[Benchmark] Frames: " << P.benchmarkFrames
                << "  Tiempo total: " << ms << " ms"
                << "  Avg por frame: " << (ms / P.benchmarkFrames) << " ms\n";

        if (gFont) TTF_CloseFont(gFont);
        TTF_Quit();
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }

    // Menú
    Mode mode = runMenu(win, ren, P);
    if (mode == Mode::QUIT) {
        if (gFont) TTF_CloseFont(gFont);
        TTF_Quit();
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }

    // Sim
    SimState S;
    initSim(S, P);

    bool running = true;
    bool showFPSPanel = false;             // <--- tecla F
    Uint64 now = SDL_GetPerformanceCounter();
    Uint64 freq = SDL_GetPerformanceFrequency();

    std::vector<float> fpsHist10; fpsHist10.reserve(10);
    std::vector<float> fpsLog;    fpsLog.reserve(300);

    while (running) {
        // dt (cap a ~33ms por estabilidad)
        Uint64 last = now; now = SDL_GetPerformanceCounter();
        float dt = float(now - last) / float(freq);
        dt = clampf(dt, 0.f, 0.033f);

        // eventos
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (e.key.keysym.sym == SDLK_r) { initSim(S, P); }
                if (e.key.keysym.sym == SDLK_f) { showFPSPanel = !showFPSPanel; } // toggle overlay
            }
        }

        // step
        step(S, P, dt);

        // FPS
        float instFPS = (dt > 0.f) ? (1.f/dt) : 0.f;
        fpsHist10.push_back(instFPS);
        if (fpsHist10.size() > 10) fpsHist10.erase(fpsHist10.begin());
        fpsLog.push_back(instFPS);
        if (fpsLog.size() > 300) fpsLog.erase(fpsLog.begin()); // guardamos los últimos 300

        // render (presentamos una sola vez al final)
        renderSim(ren, S, P, fpsHist10);
        if (showFPSPanel) {
            renderFPSOverlay(ren, fpsLog, P.width, P.height);
        }
        SDL_RenderPresent(ren);
    }

    if (gFont) TTF_CloseFont(gFont);
    TTF_Quit();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
