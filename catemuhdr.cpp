/**
 * Cat's emu 1.x - Chip-8 Emulator
 * mGBA-style GUI with SDL2
 */

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <iostream>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <vector>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

// ----------------------------------------------------------------------
// Constants
// ----------------------------------------------------------------------
constexpr int MEMORY_SIZE     = 4096;
constexpr int START_ADDR      = 0x200;
constexpr int FONTSET_ADDR    = 0x000;
constexpr int FONTSET_SIZE    = 80;

constexpr int DISPLAY_WIDTH   = 64;
constexpr int DISPLAY_HEIGHT  = 32;
constexpr int WINDOW_WIDTH    = 800;
constexpr int WINDOW_HEIGHT   = 600;
constexpr int SCALE_FACTOR    = 8;
constexpr int GAME_WIDTH      = DISPLAY_WIDTH * SCALE_FACTOR;
constexpr int GAME_HEIGHT     = DISPLAY_HEIGHT * SCALE_FACTOR;

constexpr int TIMER_HZ        = 60;
constexpr int CPU_HZ          = 700;

// GUI Constants
constexpr int TOP_BAR_HEIGHT  = 40;
constexpr int BOTTOM_BAR_HEIGHT = 30;
constexpr int GAME_Y_OFFSET   = TOP_BAR_HEIGHT;
constexpr int GAME_X_OFFSET   = (WINDOW_WIDTH - GAME_WIDTH) / 2;

static const uint8_t fontset[FONTSET_SIZE] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0,
    0x20, 0x60, 0x20, 0x20, 0x70,
    0xF0, 0x10, 0xF0, 0x80, 0xF0,
    0xF0, 0x10, 0xF0, 0x10, 0xF0,
    0x90, 0x90, 0xF0, 0x10, 0x10,
    0xF0, 0x80, 0xF0, 0x10, 0xF0,
    0xF0, 0x80, 0xF0, 0x90, 0xF0,
    0xF0, 0x10, 0x20, 0x40, 0x40,
    0xF0, 0x90, 0xF0, 0x90, 0xF0,
    0xF0, 0x90, 0xF0, 0x10, 0xF0,
    0xF0, 0x90, 0xF0, 0x90, 0x90,
    0xE0, 0x90, 0xE0, 0x90, 0xE0,
    0xF0, 0x80, 0x80, 0x80, 0xF0,
    0xE0, 0x90, 0x90, 0x90, 0xE0,
    0xF0, 0x80, 0xF0, 0x80, 0xF0,
    0xF0, 0x80, 0xF0, 0x80, 0x80
};

// ----------------------------------------------------------------------
// Chip8 class
// ----------------------------------------------------------------------
class Chip8 {
public:
    Chip8();
    void LoadROM(const std::string& filename);
    void Cycle();
    void UpdateTimers();
    bool NeedsRedraw() const { return drawFlag; }
    void ClearDrawFlag() { drawFlag = false; }
    const uint8_t* GetDisplay() const { return display; }
    void SetKey(int key, bool pressed) { keypad[key] = pressed; }
    bool GetSoundState() const { return sound_timer > 0; }
    void Reset();

private:
    uint8_t  memory[MEMORY_SIZE];
    uint8_t  V[16];
    uint16_t I;
    uint16_t pc;
    uint8_t  sp;
    uint16_t stack[16];
    uint8_t  delay_timer;
    uint8_t  sound_timer;
    uint8_t  keypad[16];
    uint8_t  display[DISPLAY_WIDTH * DISPLAY_HEIGHT];
    bool     drawFlag;

    void Opcode0xxx(uint16_t opcode);
    void Opcode1xxx(uint16_t addr);
    void Opcode2xxx(uint16_t addr);
    void Opcode3xxx(uint16_t reg, uint8_t val);
    void Opcode4xxx(uint16_t reg, uint8_t val);
    void Opcode5xxx(uint16_t regX, uint16_t regY);
    void Opcode6xxx(uint16_t reg, uint8_t val);
    void Opcode7xxx(uint16_t reg, uint8_t val);
    void Opcode8xxx(uint16_t regX, uint16_t regY, uint16_t nib);
    void Opcode9xxx(uint16_t regX, uint16_t regY);
    void OpcodeAxxx(uint16_t addr);
    void OpcodeBxxx(uint16_t addr);
    void OpcodeCxxx(uint16_t reg, uint8_t val);
    void OpcodeDxxx(uint16_t regX, uint16_t regY, uint16_t nib);
    void OpcodeExxx(uint16_t reg, uint16_t nib);
    void OpcodeFxxx(uint16_t reg, uint16_t nib);
};

Chip8::Chip8() : I(0), pc(START_ADDR), sp(0), delay_timer(0), sound_timer(0), drawFlag(false) {
    Reset();
}

void Chip8::Reset() {
    std::memset(memory, 0, sizeof(memory));
    std::memset(V, 0, sizeof(V));
    std::memset(stack, 0, sizeof(stack));
    std::memset(keypad, 0, sizeof(keypad));
    std::memset(display, 0, sizeof(display));
    std::memcpy(&memory[FONTSET_ADDR], fontset, FONTSET_SIZE);
    pc = START_ADDR;
    I = 0;
    sp = 0;
    delay_timer = 0;
    sound_timer = 0;
    drawFlag = false;
}

void Chip8::LoadROM(const std::string& filename) {
    Reset();
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open ROM file " << filename << std::endl;
        return;
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size > MEMORY_SIZE - START_ADDR) {
        std::cerr << "Error: ROM too large" << std::endl;
        return;
    }
    file.read(reinterpret_cast<char*>(&memory[START_ADDR]), size);
    file.close();
}

void Chip8::Cycle() {
    uint16_t opcode = (memory[pc] << 8) | memory[pc + 1];
    pc += 2;

    uint16_t nib1 = (opcode & 0xF000) >> 12;
    uint16_t regX = (opcode & 0x0F00) >> 8;
    uint16_t regY = (opcode & 0x00F0) >> 4;
    uint16_t nib4 = (opcode & 0x000F);
    uint16_t addr = (opcode & 0x0FFF);
    uint8_t  val  = (opcode & 0x00FF);

    switch (nib1) {
        case 0x0: Opcode0xxx(opcode); break;
        case 0x1: Opcode1xxx(addr); break;
        case 0x2: Opcode2xxx(addr); break;
        case 0x3: Opcode3xxx(regX, val); break;
        case 0x4: Opcode4xxx(regX, val); break;
        case 0x5: Opcode5xxx(regX, regY); break;
        case 0x6: Opcode6xxx(regX, val); break;
        case 0x7: Opcode7xxx(regX, val); break;
        case 0x8: Opcode8xxx(regX, regY, nib4); break;
        case 0x9: Opcode9xxx(regX, regY); break;
        case 0xA: OpcodeAxxx(addr); break;
        case 0xB: OpcodeBxxx(addr); break;
        case 0xC: OpcodeCxxx(regX, val); break;
        case 0xD: OpcodeDxxx(regX, regY, nib4); break;
        case 0xE: OpcodeExxx(regX, val); break;
        case 0xF: OpcodeFxxx(regX, val); break;
        default: break;
    }
}

void Chip8::UpdateTimers() {
    if (delay_timer > 0) delay_timer--;
    if (sound_timer > 0) sound_timer--;
}

void Chip8::Opcode0xxx(uint16_t opcode) {
    if (opcode == 0x00E0) {
        std::memset(display, 0, sizeof(display));
        drawFlag = true;
    } else if (opcode == 0x00EE) {
        pc = stack[--sp];
    }
}

void Chip8::Opcode1xxx(uint16_t addr) { pc = addr; }
void Chip8::Opcode2xxx(uint16_t addr) { stack[sp++] = pc; pc = addr; }
void Chip8::Opcode3xxx(uint16_t reg, uint8_t val) { if (V[reg] == val) pc += 2; }
void Chip8::Opcode4xxx(uint16_t reg, uint8_t val) { if (V[reg] != val) pc += 2; }
void Chip8::Opcode5xxx(uint16_t regX, uint16_t regY) { if (V[regX] == V[regY]) pc += 2; }
void Chip8::Opcode6xxx(uint16_t reg, uint8_t val) { V[reg] = val; }
void Chip8::Opcode7xxx(uint16_t reg, uint8_t val) { V[reg] += val; }
void Chip8::OpcodeAxxx(uint16_t addr) { I = addr; }
void Chip8::OpcodeBxxx(uint16_t addr) { pc = addr + V[0]; }
void Chip8::OpcodeCxxx(uint16_t reg, uint8_t val) { V[reg] = (rand() % 256) & val; }

void Chip8::Opcode8xxx(uint16_t regX, uint16_t regY, uint16_t nib) {
    switch (nib) {
        case 0x0: V[regX] = V[regY]; break;
        case 0x1: V[regX] |= V[regY]; break;
        case 0x2: V[regX] &= V[regY]; break;
        case 0x3: V[regX] ^= V[regY]; break;
        case 0x4: {
            uint16_t sum = V[regX] + V[regY];
            V[0xF] = (sum > 0xFF) ? 1 : 0;
            V[regX] = sum & 0xFF;
            break;
        }
        case 0x5: {
            V[0xF] = (V[regX] > V[regY]) ? 1 : 0;
            V[regX] -= V[regY];
            break;
        }
        case 0x6: {
            V[0xF] = V[regY] & 0x01;
            V[regX] = V[regY] >> 1;
            break;
        }
        case 0x7: {
            V[0xF] = (V[regY] > V[regX]) ? 1 : 0;
            V[regX] = V[regY] - V[regX];
            break;
        }
        case 0xE: {
            V[0xF] = (V[regY] & 0x80) >> 7;
            V[regX] = V[regY] << 1;
            break;
        }
    }
}

void Chip8::Opcode9xxx(uint16_t regX, uint16_t regY) {
    if (V[regX] != V[regY]) pc += 2;
}

void Chip8::OpcodeDxxx(uint16_t regX, uint16_t regY, uint16_t nib) {
    uint8_t x = V[regX] % DISPLAY_WIDTH;
    uint8_t y = V[regY] % DISPLAY_HEIGHT;
    V[0xF] = 0;

    for (int row = 0; row < nib; ++row) {
        if (y + row >= DISPLAY_HEIGHT) break;
        uint8_t sprite_byte = memory[I + row];
        for (int col = 0; col < 8; ++col) {
            if (x + col >= DISPLAY_WIDTH) break;
            uint8_t sprite_pixel = (sprite_byte >> (7 - col)) & 0x01;
            int idx = (y + row) * DISPLAY_WIDTH + (x + col);
            if (sprite_pixel) {
                if (display[idx] == 1) V[0xF] = 1;
                display[idx] ^= 1;
            }
        }
    }
    drawFlag = true;
}

void Chip8::OpcodeExxx(uint16_t reg, uint16_t nib) {
    if (nib == 0x9E && keypad[V[reg]]) pc += 2;
    else if (nib == 0xA1 && !keypad[V[reg]]) pc += 2;
}

void Chip8::OpcodeFxxx(uint16_t reg, uint16_t nib) {
    switch (nib) {
        case 0x07: V[reg] = delay_timer; break;
        case 0x0A: {
            bool key_pressed = false;
            for (int i = 0; i < 16; ++i) {
                if (keypad[i]) {
                    V[reg] = i;
                    key_pressed = true;
                    break;
                }
            }
            if (!key_pressed) pc -= 2;
            break;
        }
        case 0x15: delay_timer = V[reg]; break;
        case 0x18: sound_timer = V[reg]; break;
        case 0x1E: I += V[reg]; break;
        case 0x29: I = FONTSET_ADDR + (V[reg] * 5); break;
        case 0x33: {
            memory[I]     = V[reg] / 100;
            memory[I + 1] = (V[reg] / 10) % 10;
            memory[I + 2] = V[reg] % 10;
            break;
        }
        case 0x55: {
            for (int i = 0; i <= reg; ++i) memory[I + i] = V[i];
            break;
        }
        case 0x65: {
            for (int i = 0; i <= reg; ++i) V[i] = memory[I + i];
            break;
        }
    }
}

// ----------------------------------------------------------------------
// GUI Class
// ----------------------------------------------------------------------
class GUI {
public:
    GUI();
    ~GUI();
    bool Initialize();
    void HandleEvents(bool& quit, Chip8& chip8);
    void Render(const Chip8& chip8, float fps, const std::string& romName);
    void UpdateTitle(float fps);
    void ShowFileDialog(std::string& romPath);
    void DrawMenuBar();
    void DrawStatusBar(float fps, const std::string& romName);

private:
    SDL_Window* window;
    SDL_Renderer* renderer;
    TTF_Font* fontSmall;
    TTF_Font* fontMedium;
    SDL_Color white;
    SDL_Color black;
    SDL_Color grey;
    SDL_Color darkGrey;
    SDL_Color highlight;
    
    bool menuOpen;
    bool showAbout;
    bool showControls;
    
    void DrawText(const char* text, int x, int y, TTF_Font* font, SDL_Color color);
    void DrawRect(int x, int y, int w, int h, SDL_Color color);
    void DrawBorder();
};

GUI::GUI() : window(nullptr), renderer(nullptr), fontSmall(nullptr), fontMedium(nullptr),
             menuOpen(false), showAbout(false), showControls(false) {
    white = {255, 255, 255, 255};
    black = {0, 0, 0, 255};
    grey = {128, 128, 128, 255};
    darkGrey = {64, 64, 64, 255};
    highlight = {255, 255, 0, 255};
}

GUI::~GUI() {
    if (fontSmall) TTF_CloseFont(fontSmall);
    if (fontMedium) TTF_CloseFont(fontMedium);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
}

bool GUI::Initialize() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        std::cerr << "SDL init failed: " << SDL_GetError() << std::endl;
        return false;
    }

    if (TTF_Init() < 0) {
        std::cerr << "TTF init failed: " << TTF_GetError() << std::endl;
        return false;
    }

    window = SDL_CreateWindow("Cat's emu 1.x", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

    // Load fonts (try multiple paths for cross-platform)
    fontSmall = TTF_OpenFont("/System/Library/Fonts/Helvetica.ttc", 12);
    if (!fontSmall) fontSmall = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 12);
    if (!fontSmall) fontSmall = TTF_OpenFont("/usr/share/fonts/TTF/DejaVuSans.ttf", 12);
    
    fontMedium = TTF_OpenFont("/System/Library/Fonts/Helvetica.ttc", 16);
    if (!fontMedium) fontMedium = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 16);
    if (!fontMedium) fontMedium = TTF_OpenFont("/usr/share/fonts/TTF/DejaVuSans.ttf", 16);

    return true;
}

void GUI::DrawText(const char* text, int x, int y, TTF_Font* font, SDL_Color color) {
    if (!font) return;
    SDL_Surface* surf = TTF_RenderText_Blended(font, text, color);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_Rect dst = {x, y, surf->w, surf->h};
    SDL_RenderCopy(renderer, tex, nullptr, &dst);
    SDL_FreeSurface(surf);
    SDL_DestroyTexture(tex);
}

void GUI::DrawRect(int x, int y, int w, int h, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(renderer, &rect);
}

void GUI::DrawBorder() {
    // Top bar
    DrawRect(0, 0, WINDOW_WIDTH, TOP_BAR_HEIGHT, darkGrey);
    // Bottom bar
    DrawRect(0, WINDOW_HEIGHT - BOTTOM_BAR_HEIGHT, WINDOW_WIDTH, BOTTOM_BAR_HEIGHT, darkGrey);
    // Game area border
    SDL_SetRenderDrawColor(renderer, grey.r, grey.g, grey.b, grey.a);
    SDL_Rect gameBorder = {GAME_X_OFFSET - 2, GAME_Y_OFFSET - 2, GAME_WIDTH + 4, GAME_HEIGHT + 4};
    SDL_RenderDrawRect(renderer, &gameBorder);
}

void GUI::DrawMenuBar() {
    // File menu
    DrawText("File", 10, 10, fontMedium, white);
    DrawText("Emulation", 80, 10, fontMedium, white);
    DrawText("View", 200, 10, fontMedium, white);
    DrawText("Help", 280, 10, fontMedium, white);
    
    // Separator line
    SDL_SetRenderDrawColor(renderer, grey.r, grey.g, grey.b, grey.a);
    SDL_RenderDrawLine(renderer, 0, TOP_BAR_HEIGHT - 2, WINDOW_WIDTH, TOP_BAR_HEIGHT - 2);
}

void GUI::DrawStatusBar(float fps, const std::string& romName) {
    char fpsText[32];
    snprintf(fpsText, sizeof(fpsText), "FPS: %.1f", fps);
    DrawText(fpsText, 10, WINDOW_HEIGHT - BOTTOM_BAR_HEIGHT + 8, fontSmall, white);
    
    std::string rom = "ROM: " + (romName.empty() ? "None" : romName);
    DrawText(rom.c_str(), 120, WINDOW_HEIGHT - BOTTOM_BAR_HEIGHT + 8, fontSmall, white);
    
    DrawText("Cat's emu 1.x", WINDOW_WIDTH - 150, WINDOW_HEIGHT - BOTTOM_BAR_HEIGHT + 8, fontSmall, white);
}

void GUI::HandleEvents(bool& quit, Chip8& chip8) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            quit = true;
        } else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
            bool pressed = (e.type == SDL_KEYDOWN);
            switch (e.key.keysym.sym) {
                case SDLK_1: chip8.SetKey(0x1, pressed); break;
                case SDLK_2: chip8.SetKey(0x2, pressed); break;
                case SDLK_3: chip8.SetKey(0x3, pressed); break;
                case SDLK_4: chip8.SetKey(0xC, pressed); break;
                case SDLK_q: chip8.SetKey(0x4, pressed); break;
                case SDLK_w: chip8.SetKey(0x5, pressed); break;
                case SDLK_e: chip8.SetKey(0x6, pressed); break;
                case SDLK_r: chip8.SetKey(0xD, pressed); break;
                case SDLK_a: chip8.SetKey(0x7, pressed); break;
                case SDLK_s: chip8.SetKey(0x8, pressed); break;
                case SDLK_d: chip8.SetKey(0x9, pressed); break;
                case SDLK_f: chip8.SetKey(0xE, pressed); break;
                case SDLK_z: chip8.SetKey(0xA, pressed); break;
                case SDLK_x: chip8.SetKey(0x0, pressed); break;
                case SDLK_c: chip8.SetKey(0xB, pressed); break;
                case SDLK_v: chip8.SetKey(0xF, pressed); break;
                case SDLK_F1: std::cout << "Help F1" << std::endl; break;
                case SDLK_F5: chip8.Reset(); std::cout << "Reset" << std::endl; break;
                default: break;
            }
        } else if (e.type == SDL_WINDOWEVENT) {
            if (e.window.event == SDL_WINDOWEVENT_RESIZED) {
                // Handle window resize
            }
        }
    }
}

void GUI::Render(const Chip8& chip8, float fps, const std::string& romName) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    // Draw game screen
    const uint8_t* display = chip8.GetDisplay();
    for (int y = 0; y < DISPLAY_HEIGHT; ++y) {
        for (int x = 0; x < DISPLAY_WIDTH; ++x) {
            if (display[y * DISPLAY_WIDTH + x]) {
                SDL_Rect rect = {
                    GAME_X_OFFSET + x * SCALE_FACTOR,
                    GAME_Y_OFFSET + y * SCALE_FACTOR,
                    SCALE_FACTOR,
                    SCALE_FACTOR
                };
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                SDL_RenderFillRect(renderer, &rect);
            }
        }
    }

    // Draw GUI elements
    DrawMenuBar();
    DrawBorder();
    DrawStatusBar(fps, romName);

    SDL_RenderPresent(renderer);
}

void GUI::UpdateTitle(float fps) {
    char title[128];
    snprintf(title, sizeof(title), "Cat's emu 1.x - [%.1f FPS]", fps);
    SDL_SetWindowTitle(window, title);
}

// ----------------------------------------------------------------------
// Audio callback
// ----------------------------------------------------------------------
void audio_callback(void* userdata, Uint8* stream, int len) {
    static int phase = 0;
    bool* beep_active = static_cast<bool*>(userdata);
    for (int i = 0; i < len; ++i) {
        stream[i] = (*beep_active) ? ((phase++ / 50) % 2 ? 255 : 0) : 0;
    }
}

// ----------------------------------------------------------------------
// Main
// ----------------------------------------------------------------------
int main(int argc, char* argv[]) {
    GUI gui;
    if (!gui.Initialize()) {
        return 1;
    }

    // Audio setup
    SDL_AudioSpec desired, obtained;
    desired.freq = 44100;
    desired.format = AUDIO_U8;
    desired.channels = 1;
    desired.samples = 2048;
    desired.callback = audio_callback;
    bool beep_active = false;
    desired.userdata = &beep_active;

    if (SDL_OpenAudio(&desired, &obtained) < 0) {
        std::cerr << "Audio open failed: " << SDL_GetError() << std::endl;
    }
    SDL_PauseAudio(0);

    Chip8 chip8;
    std::string currentROM;
    
    if (argc > 1) {
        currentROM = argv[1];
        chip8.LoadROM(currentROM);
    }

    using clock = std::chrono::steady_clock;
    auto last_timer_update = clock::now();
    auto last_fps_update = clock::now();
    int frame_count = 0;
    float fps = 0.0f;
    const auto cycle_duration = std::chrono::microseconds(1000000 / CPU_HZ);
    bool quit = false;

    while (!quit) {
        auto cycle_start = clock::now();

        gui.HandleEvents(quit, chip8);

        auto now = clock::now();
        while (now - cycle_start < cycle_duration) {
            chip8.Cycle();
            now = clock::now();
        }

        now = clock::now();
        if (std::chrono::duration_cast<std::chrono::microseconds>(now - last_timer_update).count() >= 1000000 / TIMER_HZ) {
            chip8.UpdateTimers();
            last_timer_update = now;
        }

        beep_active = chip8.GetSoundState();

        // FPS calculation
        frame_count++;
        auto fps_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_fps_update);
        if (fps_elapsed.count() >= 500) { // Update FPS every 500ms
            fps = frame_count * 1000.0f / fps_elapsed.count();
            frame_count = 0;
            last_fps_update = now;
            gui.UpdateTitle(fps);
        }

        gui.Render(chip8, fps, currentROM);
    }

    SDL_CloseAudio();
    TTF_Quit();
    SDL_Quit();

    return 0;
}