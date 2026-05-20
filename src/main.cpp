#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include <dirent.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

const int START_W = 1000;
const int START_H = 650;
const int REFRESH_MS = 500;

struct Color {
    Uint8 r, g, b, a;
};

const Color BG{15, 23, 42, 255};
const Color PANEL{30, 41, 59, 255};
const Color PANEL_2{51, 65, 85, 255};
const Color LINE{71, 85, 105, 255};
const Color TEXT{241, 245, 249, 255};
const Color MUTED{148, 163, 184, 255};
const Color BLUE{56, 189, 248, 255};
const Color GREEN{45, 212, 191, 255};
const Color YELLOW{251, 191, 36, 255};

struct Process {
    int pid = 0;
    string name;
    string state;
    string command;
    long memoryKb = 0;
    int threads = 0;
    long long cpuTicks = 0;
    double cpuPercent = 0.0;
};

struct CpuSample {
    long long total = 0;
    long long idle = 0;
};

enum SortColumn {
    SORT_CPU,
    SORT_MEMORY,
    SORT_PID,
    SORT_NAME,
    SORT_THREADS,
    SORT_STATE
};

struct Fonts {
    TTF_Font *title = nullptr;
    TTF_Font *big = nullptr;
    TTF_Font *normal = nullptr;
    TTF_Font *small = nullptr;
};

struct Column {
    SortColumn sort;
    string label;
    int x;
    int w;
};

vector<Process> processes;
map<int, long long> previousTicks;
CpuSample previousCpu;
double cpuPercent = 0.0;
long memoryTotalKb = 0;
long memoryAvailableKb = 0;
bool liveProcData = false;
bool paused = false;
int scrollRow = 0;
int selectedPid = -1;
SortColumn sortColumn = SORT_CPU;
bool sortDescending = true;
Uint32 lastRefresh = 0;
int coreCount = 1;

void setColor(SDL_Renderer *renderer, Color c) {
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
}

SDL_Color sdlColor(Color c) {
    return SDL_Color{c.r, c.g, c.b, c.a};
}

void fill(SDL_Renderer *renderer, SDL_Rect r, Color c) {
    setColor(renderer, c);
    SDL_RenderFillRect(renderer, &r);
}

void outline(SDL_Renderer *renderer, SDL_Rect r, Color c) {
    setColor(renderer, c);
    SDL_RenderDrawRect(renderer, &r);
}

void line(SDL_Renderer *renderer, int x1, int y1, int x2, int y2, Color c) {
    setColor(renderer, c);
    SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
}

string readFile(const string &path) {
    ifstream file(path, ios::binary);
    if (!file) {
        return "";
    }
    return string(istreambuf_iterator<char>(file), istreambuf_iterator<char>());
}

bool isNumber(const string &s) {
    if (s.empty()) {
        return false;
    }
    for (char c : s) {
        if (!isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

string oneDecimal(double value) {
    stringstream out;
    out << fixed << setprecision(1) << value;
    return out.str();
}

string memoryText(long kb) {
    if (kb > 1024 * 1024) {
        return oneDecimal(kb / 1024.0 / 1024.0) + " GB";
    }
    return oneDecimal(kb / 1024.0) + " MB";
}

string stateText(const string &s) {
    if (s == "R") return "Running";
    if (s == "S") return "Sleep";
    if (s == "D") return "Disk wait";
    if (s == "T") return "Stopped";
    if (s == "Z") return "Zombie";
    return s.empty() ? "?" : s;
}

int textWidth(TTF_Font *font, const string &text) {
    int w = 0;
    TTF_SizeUTF8(font, text.c_str(), &w, nullptr);
    return w;
}

string trimToWidth(TTF_Font *font, string text, int maxWidth) {
    if (maxWidth <= 0) {
        return "";
    }
    if (textWidth(font, text) <= maxWidth) {
        return text;
    }
    while (!text.empty() && textWidth(font, text + "...") > maxWidth) {
        text.pop_back();
    }
    return text + "...";
}

void drawText(SDL_Renderer *renderer, TTF_Font *font, string text, int x, int y, Color color, int maxWidth = -1) {
    if (text.empty()) {
        return;
    }
    if (maxWidth > 0) {
        text = trimToWidth(font, text, maxWidth);
    }

    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text.c_str(), sdlColor(color));
    if (!surface) {
        return;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_Rect dst{x, y, surface->w, surface->h};
        SDL_RenderCopy(renderer, texture, nullptr, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

void drawRightText(SDL_Renderer *renderer, TTF_Font *font, const string &text, int rightX, int y, Color color) {
    drawText(renderer, font, text, rightX - textWidth(font, text), y, color);
}

void drawBar(SDL_Renderer *renderer, SDL_Rect r, double percent, Color color) {
    percent = max(0.0, min(100.0, percent));
    fill(renderer, r, PANEL_2);
    SDL_Rect used = r;
    used.w = static_cast<int>(r.w * percent / 100.0);
    fill(renderer, used, color);
    outline(renderer, r, LINE);
}

string findFont() {
    vector<string> choices = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        "/Library/Fonts/Arial Unicode.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf"
    };

    for (const string &path : choices) {
        ifstream test(path);
        if (test.good()) {
            return path;
        }
    }
    return "";
}

bool loadFonts(Fonts &fonts) {
    string fontPath = findFont();
    if (fontPath.empty()) {
        cerr << "Could not find a font. Install DejaVu Sans or Liberation Sans.\n";
        return false;
    }

    fonts.title = TTF_OpenFont(fontPath.c_str(), 28);
    fonts.big = TTF_OpenFont(fontPath.c_str(), 18);
    fonts.normal = TTF_OpenFont(fontPath.c_str(), 14);
    fonts.small = TTF_OpenFont(fontPath.c_str(), 12);

    if (!fonts.title || !fonts.big || !fonts.normal || !fonts.small) {
        cerr << "Font load error: " << TTF_GetError() << "\n";
        return false;
    }

    TTF_SetFontStyle(fonts.title, TTF_STYLE_BOLD);
    TTF_SetFontStyle(fonts.big, TTF_STYLE_BOLD);
    return true;
}

void closeFonts(Fonts &fonts) {
    if (fonts.title) TTF_CloseFont(fonts.title);
    if (fonts.big) TTF_CloseFont(fonts.big);
    if (fonts.normal) TTF_CloseFont(fonts.normal);
    if (fonts.small) TTF_CloseFont(fonts.small);
}

SDL_Texture *loadImage(SDL_Renderer *renderer) {
    vector<string> paths = {"assets/chip.xpm", "../assets/chip.xpm"};
    for (const string &path : paths) {
        SDL_Surface *surface = IMG_Load(path.c_str());
        if (surface) {
            SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
            SDL_FreeSurface(surface);
            return texture;
        }
    }

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 32, 32, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surface) {
        return nullptr;
    }

    SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, 15, 23, 42, 255));
    SDL_Rect chipBody{6, 6, 20, 20};
    SDL_Rect chipCore{12, 12, 8, 8};
    SDL_FillRect(surface, &chipBody, SDL_MapRGBA(surface->format, 45, 212, 191, 255));
    SDL_FillRect(surface, &chipCore, SDL_MapRGBA(surface->format, 241, 245, 249, 255));

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    return texture;
}

CpuSample readCpu() {
    ifstream file("/proc/stat");
    string cpu;
    long long user = 0, nice = 0, sys = 0, idle = 0, iowait = 0;
    long long irq = 0, softirq = 0, steal = 0;
    file >> cpu >> user >> nice >> sys >> idle >> iowait >> irq >> softirq >> steal;

    CpuSample sample;
    sample.idle = idle + iowait;
    sample.total = user + nice + sys + idle + iowait + irq + softirq + steal;
    return sample;
}

void readMemory() {
    ifstream file("/proc/meminfo");
    string key, unit;
    long value;
    memoryTotalKb = 0;
    memoryAvailableKb = 0;

    while (file >> key >> value >> unit) {
        if (key == "MemTotal:") {
            memoryTotalKb = value;
        } else if (key == "MemAvailable:") {
            memoryAvailableKb = value;
        }
    }
}

bool readProcessStat(int pid, Process &p) {
    string stat = readFile("/proc/" + to_string(pid) + "/stat");
    size_t open = stat.find('(');
    size_t close = stat.rfind(')');
    if (open == string::npos || close == string::npos) {
        return false;
    }

    p.name = stat.substr(open + 1, close - open - 1);
    stringstream rest(stat.substr(close + 2));
    rest >> p.state;

    vector<long long> numbers;
    long long n;
    while (rest >> n) {
        numbers.push_back(n);
    }

    if (numbers.size() > 11) {
        p.cpuTicks = numbers[10] + numbers[11];
    }
    return true;
}

void readProcessStatus(int pid, Process &p) {
    ifstream file("/proc/" + to_string(pid) + "/status");
    string lineText;

    while (getline(file, lineText)) {
        if (lineText.rfind("VmRSS:", 0) == 0) {
            stringstream line(lineText.substr(6));
            line >> p.memoryKb;
        } else if (lineText.rfind("Threads:", 0) == 0) {
            stringstream line(lineText.substr(8));
            line >> p.threads;
        }
    }
}

vector<Process> demoProcesses() {
    vector<Process> demo;
    vector<string> names = {"terminal", "browser", "compiler", "shell", "audio", "editor", "logger", "network"};
    for (int i = 0; i < static_cast<int>(names.size()); i++) {
        Process p;
        p.pid = 1000 + i * 9;
        p.name = names[i];
        p.state = (i % 3 == 0) ? "R" : "S";
        p.cpuPercent = 5.0 + (i * 8) % 45;
        p.memoryKb = 20000 + i * 52000;
        p.threads = 1 + i % 6;
        p.command = "/usr/bin/" + p.name + " --demo";
        demo.push_back(p);
    }
    memoryTotalKb = 16L * 1024L * 1024L;
    memoryAvailableKb = 7L * 1024L * 1024L;
    cpuPercent = 42.0;
    liveProcData = false;
    return demo;
}

vector<Process> readProcesses() {
    DIR *dir = opendir("/proc");
    if (!dir) {
        return demoProcesses();
    }

    liveProcData = true;
    readMemory();
    CpuSample nowCpu = readCpu();
    long long cpuDelta = previousCpu.total == 0 ? 0 : nowCpu.total - previousCpu.total;
    long long idleDelta = previousCpu.total == 0 ? 0 : nowCpu.idle - previousCpu.idle;
    if (cpuDelta > 0) {
        cpuPercent = (cpuDelta - idleDelta) * 100.0 / cpuDelta;
    }

    map<int, long long> newTicks;
    vector<Process> result;

    dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        string folder = entry->d_name;
        if (!isNumber(folder)) {
            continue;
        }

        int pid = atoi(folder.c_str());
        Process p;
        p.pid = pid;
        if (!readProcessStat(pid, p)) {
            continue;
        }
        readProcessStatus(pid, p);

        p.command = readFile("/proc/" + folder + "/cmdline");
        replace(p.command.begin(), p.command.end(), '\0', ' ');
        if (p.command.empty()) {
            p.command = "[" + p.name + "]";
        }

        newTicks[p.pid] = p.cpuTicks;
        if (previousTicks.count(p.pid) && cpuDelta > 0) {
            long long processDelta = p.cpuTicks - previousTicks[p.pid];
            p.cpuPercent = processDelta * 100.0 * coreCount / cpuDelta;
            if (p.cpuPercent < 0) {
                p.cpuPercent = 0;
            }
        }

        result.push_back(p);
    }

    closedir(dir);
    previousTicks = newTicks;
    previousCpu = nowCpu;
    return result;
}

bool compareProcesses(const Process &a, const Process &b) {
    if (sortColumn == SORT_CPU) {
        if (a.cpuPercent != b.cpuPercent) return sortDescending ? a.cpuPercent > b.cpuPercent : a.cpuPercent < b.cpuPercent;
    } else if (sortColumn == SORT_MEMORY) {
        if (a.memoryKb != b.memoryKb) return sortDescending ? a.memoryKb > b.memoryKb : a.memoryKb < b.memoryKb;
    } else if (sortColumn == SORT_PID) {
        if (a.pid != b.pid) return sortDescending ? a.pid > b.pid : a.pid < b.pid;
    } else if (sortColumn == SORT_NAME) {
        if (a.name != b.name) return sortDescending ? a.name > b.name : a.name < b.name;
    } else if (sortColumn == SORT_THREADS) {
        if (a.threads != b.threads) return sortDescending ? a.threads > b.threads : a.threads < b.threads;
    } else if (sortColumn == SORT_STATE) {
        if (a.state != b.state) return sortDescending ? a.state > b.state : a.state < b.state;
    }

    return a.pid < b.pid;
}

void sortProcessList() {
    sort(processes.begin(), processes.end(), compareProcesses);
}

int selectedIndex() {
    for (int i = 0; i < static_cast<int>(processes.size()); i++) {
        if (processes[i].pid == selectedPid) {
            return i;
        }
    }
    return -1;
}

vector<Column> tableColumns(int width) {
    int tableX = 16;
    int fixed = 74 + 74 + 90 + 105 + 86;
    int nameW = max(160, width - 32 - fixed);

    vector<Column> cols;
    int x = tableX;
    cols.push_back({SORT_PID, "PID", x, 74});
    x += 74;
    cols.push_back({SORT_NAME, "Name", x, nameW});
    x += nameW;
    cols.push_back({SORT_CPU, "CPU %", x, 90});
    x += 90;
    cols.push_back({SORT_MEMORY, "Memory", x, 105});
    x += 105;
    cols.push_back({SORT_THREADS, "Threads", x, 86});
    x += 86;
    cols.push_back({SORT_STATE, "State", x, 74});
    return cols;
}

void clampScroll(int windowH) {
    int tableY = 206;
    int bottomY = windowH - 76;
    int visibleRows = max(1, (bottomY - tableY - 30) / 28);
    int maxScroll = max(0, static_cast<int>(processes.size()) - visibleRows);
    scrollRow = max(0, min(scrollRow, maxScroll));
}

void refreshData() {
    processes = readProcesses();
    sortProcessList();

    if (!processes.empty() && selectedIndex() == -1) {
        selectedPid = processes[0].pid;
    }

    lastRefresh = SDL_GetTicks();
}

void drawHeader(SDL_Renderer *renderer, const Fonts &fonts, SDL_Texture *chip) {
    if (chip) {
        SDL_Rect icon{18, 18, 46, 46};
        SDL_RenderCopy(renderer, chip, nullptr, &icon);
    }

    drawText(renderer, fonts.title, "OS Process Monitor", 78, 18, TEXT);
    string source = liveProcData ? "Reading live Linux /proc process data" : "Demo data because /proc was not found";
    string status = paused ? "paused" : "refreshes every 500 ms";
    drawText(renderer, fonts.small, source + " - " + status, 80, 52, MUTED);
}

void drawStats(SDL_Renderer *renderer, const Fonts &fonts, int width) {
    int y = 86;
    int cardW = (width - 44) / 2;

    fill(renderer, SDL_Rect{16, y, cardW, 84}, PANEL);
    outline(renderer, SDL_Rect{16, y, cardW, 84}, LINE);
    drawText(renderer, fonts.small, "CPU Usage", 30, y + 10, MUTED);
    drawText(renderer, fonts.big, oneDecimal(cpuPercent) + "%", 30, y + 30, TEXT);
    drawBar(renderer, SDL_Rect{30, y + 62, cardW - 28, 10}, cpuPercent, BLUE);

    double memUsed = 0.0;
    if (memoryTotalKb > 0) {
        memUsed = (memoryTotalKb - memoryAvailableKb) * 100.0 / memoryTotalKb;
    }

    int x = 28 + cardW;
    fill(renderer, SDL_Rect{x, y, cardW, 84}, PANEL);
    outline(renderer, SDL_Rect{x, y, cardW, 84}, LINE);
    drawText(renderer, fonts.small, "Memory Usage", x + 14, y + 10, MUTED);
    drawText(renderer, fonts.big, memoryText(memoryTotalKb - memoryAvailableKb) + " / " + memoryText(memoryTotalKb), x + 14, y + 30, TEXT, cardW - 28);
    drawBar(renderer, SDL_Rect{x + 14, y + 62, cardW - 28, 10}, memUsed, GREEN);

    drawText(renderer, fonts.small, to_string(processes.size()) + " processes", 18, 178, MUTED);
    drawRightText(renderer, fonts.small, "Click headers to sort | Scroll wheel moves list | Space pauses | R refreshes", width - 18, 178, MUTED);
}

void drawTable(SDL_Renderer *renderer, const Fonts &fonts, int width, int height) {
    int tableX = 16;
    int tableY = 206;
    int tableW = width - 32;
    int bottomY = height - 76;
    int rowH = 28;
    int headerH = 30;

    fill(renderer, SDL_Rect{tableX, tableY, tableW, bottomY - tableY}, PANEL);
    outline(renderer, SDL_Rect{tableX, tableY, tableW, bottomY - tableY}, LINE);
    fill(renderer, SDL_Rect{tableX, tableY, tableW, headerH}, PANEL_2);

    vector<Column> cols = tableColumns(width);
    for (const Column &c : cols) {
        string label = c.label;
        if (c.sort == sortColumn) {
            label += sortDescending ? " v" : " ^";
        }
        drawText(renderer, fonts.small, label, c.x + 8, tableY + 8, TEXT, c.w - 12);
        line(renderer, c.x + c.w, tableY, c.x + c.w, bottomY, LINE);
    }
    line(renderer, tableX, tableY + headerH, tableX + tableW, tableY + headerH, LINE);

    int visibleRows = max(1, (bottomY - tableY - headerH) / rowH);
    int end = min(static_cast<int>(processes.size()), scrollRow + visibleRows);
    int selected = selectedIndex();

    for (int i = scrollRow; i < end; i++) {
        Process &p = processes[i];
        int y = tableY + headerH + (i - scrollRow) * rowH;

        if (i == selected) {
            fill(renderer, SDL_Rect{tableX, y, tableW, rowH}, Color{14, 116, 144, 255});
        } else if ((i - scrollRow) % 2 == 0) {
            fill(renderer, SDL_Rect{tableX, y, tableW, rowH}, Color{24, 34, 52, 255});
        }

        Color cpuColor = p.cpuPercent > 50 ? YELLOW : BLUE;
        drawText(renderer, fonts.small, to_string(p.pid), cols[0].x + 8, y + 7, TEXT, cols[0].w - 12);
        drawText(renderer, fonts.small, p.name, cols[1].x + 8, y + 7, TEXT, cols[1].w - 12);
        drawText(renderer, fonts.small, oneDecimal(p.cpuPercent), cols[2].x + 8, y + 7, cpuColor, cols[2].w - 12);
        drawText(renderer, fonts.small, memoryText(p.memoryKb), cols[3].x + 8, y + 7, TEXT, cols[3].w - 12);
        drawText(renderer, fonts.small, to_string(p.threads), cols[4].x + 8, y + 7, TEXT, cols[4].w - 12);
        drawText(renderer, fonts.small, stateText(p.state), cols[5].x + 8, y + 7, MUTED, cols[5].w - 12);
    }
}

void drawSelectedProcess(SDL_Renderer *renderer, const Fonts &fonts, int width, int height) {
    int y = height - 60;
    fill(renderer, SDL_Rect{16, y, width - 32, 44}, PANEL);
    outline(renderer, SDL_Rect{16, y, width - 32, 44}, LINE);

    int index = selectedIndex();
    if (index == -1) {
        drawText(renderer, fonts.normal, "Select a process row to view its command.", 28, y + 13, MUTED);
        return;
    }

    Process &p = processes[index];
    string details = "Selected PID " + to_string(p.pid) + " | " + p.name + " | " + p.command;
    drawText(renderer, fonts.normal, details, 28, y + 13, TEXT, width - 56);
}

void render(SDL_Renderer *renderer, const Fonts &fonts, SDL_Texture *chip, int width, int height) {
    setColor(renderer, BG);
    SDL_RenderClear(renderer);

    drawHeader(renderer, fonts, chip);
    drawStats(renderer, fonts, width);
    drawTable(renderer, fonts, width, height);
    drawSelectedProcess(renderer, fonts, width, height);

    SDL_RenderPresent(renderer);
}

void handleHeaderClick(int x, int y, int width) {
    if (y < 206 || y > 236) {
        return;
    }

    for (const Column &c : tableColumns(width)) {
        if (x >= c.x && x <= c.x + c.w) {
            if (sortColumn == c.sort) {
                sortDescending = !sortDescending;
            } else {
                sortColumn = c.sort;
                sortDescending = (c.sort == SORT_CPU || c.sort == SORT_MEMORY || c.sort == SORT_THREADS);
            }
            sortProcessList();
            return;
        }
    }
}

void handleRowClick(int x, int y, int width, int height) {
    int tableY = 206;
    int bottomY = height - 76;
    if (x < 16 || x > width - 16 || y < tableY + 30 || y > bottomY) {
        return;
    }

    int row = (y - tableY - 30) / 28;
    int index = scrollRow + row;
    if (index >= 0 && index < static_cast<int>(processes.size())) {
        selectedPid = processes[index].pid;
    }
}

void moveSelection(int amount, int height) {
    if (processes.empty()) {
        return;
    }

    int index = selectedIndex();
    if (index == -1) {
        index = 0;
    }
    index = max(0, min(static_cast<int>(processes.size()) - 1, index + amount));
    selectedPid = processes[index].pid;

    int visibleRows = max(1, (height - 76 - 206 - 30) / 28);
    if (index < scrollRow) {
        scrollRow = index;
    } else if (index >= scrollRow + visibleRows) {
        scrollRow = index - visibleRows + 1;
    }
}

int main(int, char **) {
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    coreCount = cores > 0 ? static_cast<int>(cores) : 1;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        cerr << "SDL error: " << SDL_GetError() << "\n";
        return 1;
    }
    if (TTF_Init() != 0) {
        cerr << "SDL_ttf error: " << TTF_GetError() << "\n";
        SDL_Quit();
        return 1;
    }
    IMG_Init(IMG_INIT_PNG);

    SDL_Window *window = SDL_CreateWindow("OS Process Monitor",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          START_W,
                                          START_H,
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        cerr << "Window error: " << SDL_GetError() << "\n";
        IMG_Quit();
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) {
        cerr << "Renderer error: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        IMG_Quit();
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    Fonts fonts;
    if (!loadFonts(fonts)) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        IMG_Quit();
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Texture *chip = loadImage(renderer);
    refreshData();

    bool running = true;
    while (running) {
        int width, height;
        SDL_GetWindowSize(window, &width, &height);

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                handleHeaderClick(event.button.x, event.button.y, width);
                handleRowClick(event.button.x, event.button.y, width, height);
            } else if (event.type == SDL_MOUSEWHEEL) {
                scrollRow -= event.wheel.y * 3;
                clampScroll(height);
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                } else if (event.key.keysym.sym == SDLK_SPACE) {
                    paused = !paused;
                } else if (event.key.keysym.sym == SDLK_r) {
                    refreshData();
                } else if (event.key.keysym.sym == SDLK_DOWN) {
                    moveSelection(1, height);
                } else if (event.key.keysym.sym == SDLK_UP) {
                    moveSelection(-1, height);
                } else if (event.key.keysym.sym == SDLK_PAGEDOWN) {
                    moveSelection(10, height);
                } else if (event.key.keysym.sym == SDLK_PAGEUP) {
                    moveSelection(-10, height);
                }
            }
        }

        if (!paused && SDL_GetTicks() - lastRefresh >= REFRESH_MS) {
            refreshData();
        }

        clampScroll(height);
        render(renderer, fonts, chip, width, height);
        SDL_Delay(16);
    }

    if (chip) {
        SDL_DestroyTexture(chip);
    }
    closeFonts(fonts);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    return 0;
}
