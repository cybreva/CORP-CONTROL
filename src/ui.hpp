// ui.hpp - terminal UI toolkit: raw keys, menus, tables, forms, cards (ANSI, cross-platform)
#pragma once
#include "util.hpp"
#include <chrono>
#include <cstdio>
#ifdef _WIN32
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #include <windows.h>
  #include <conio.h>
  #include <io.h>
#else
  #include <termios.h>
  #include <unistd.h>
  #include <sys/ioctl.h>
  #include <sys/select.h>
  #include <signal.h>
#endif

namespace ui {
using util::rep;
struct SessionTimeout {};

namespace c {
constexpr const char* R = "\x1b[0m";   constexpr const char* BOLD = "\x1b[1m"; constexpr const char* DIM = "\x1b[2m";
constexpr const char* ACC = "\x1b[38;5;81m";  constexpr const char* ACC2 = "\x1b[38;5;141m";
constexpr const char* OK = "\x1b[38;5;78m";   constexpr const char* WARN = "\x1b[38;5;214m";
constexpr const char* BAD = "\x1b[38;5;203m"; constexpr const char* MUTE = "\x1b[38;5;244m";
constexpr const char* SEL = "\x1b[48;5;24m\x1b[38;5;255m\x1b[1m";
}
inline std::string paint(const char* col, const std::string& s) { return std::string(col) + s + c::R; }

// ---------- text measuring ----------
inline int vlen(const std::string& s) {
    int n = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char ch = (unsigned char)s[i];
        if (ch == 0x1b) { i++; if (i < s.size() && s[i] == '[') { i++; while (i < s.size() && !(s[i] >= '@' && s[i] <= '~')) i++; if (i < s.size()) i++; } continue; }
        if ((ch & 0xC0) != 0x80) n++; i++;
    }
    return n;
}
inline std::string strip(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == 0x1b) { i++; if (i < s.size() && s[i] == '[') { i++; while (i < s.size() && !(s[i] >= '@' && s[i] <= '~')) i++; if (i < s.size()) i++; } continue; }
        o += s[i++];
    }
    return o;
}
inline std::string clip(const std::string& s, int w) {
    if (w <= 0) return ""; if (vlen(s) <= w) return s;
    std::string o; int n = 0; bool esc = false;
    for (size_t i = 0; i < s.size();) {
        unsigned char ch = (unsigned char)s[i];
        if (ch == 0x1b) { size_t j = i + 1; if (j < s.size() && s[j] == '[') { j++; while (j < s.size() && !(s[j] >= '@' && s[j] <= '~')) j++; if (j < s.size()) j++; } o += s.substr(i, j - i); i = j; esc = true; continue; }
        size_t len = ch >= 0xF0 ? 4 : ch >= 0xE0 ? 3 : ch >= 0xC0 ? 2 : 1;
        if (n >= w - 1) break; o += s.substr(i, len); i += len; n++;
    }
    o += "…"; if (esc) o += c::R; return o;
}
inline std::string padR(const std::string& s, int w) { std::string t = clip(s, w); return t + rep(" ", std::max(0, w - vlen(t))); }
inline std::string padL(const std::string& s, int w) { std::string t = clip(s, w); return rep(" ", std::max(0, w - vlen(t))) + t; }
inline std::vector<std::string> wrap(const std::string& text, int w) {
    std::vector<std::string> out;
    for (auto& para : util::split(text, '\n')) {
        std::string cur; std::istringstream is(para); std::string word;
        while (is >> word) { if (!cur.empty() && vlen(cur) + 1 + vlen(word) > w) { out.push_back(cur); cur.clear(); } if (!cur.empty()) cur += ' '; cur += word; }
        out.push_back(cur);
    }
    return out;
}

// ---------- terminal ----------
enum class K { Char, Enter, Esc, Up, Down, Left, Right, Home, End, PgUp, PgDn, Del, Back, Tab };
struct Key { K k; std::string text; bool is(char ch) const { return k == K::Char && text.size() == 1 && tolower((unsigned char)text[0]) == tolower((unsigned char)ch); } };

namespace term {
inline bool& done() { static bool d = false; return d; }
inline int& idleLimit() { static int v = 0; return v; } // seconds, 0 = off
inline void out(const std::string& s) { fwrite(s.data(), 1, s.size(), stdout); fflush(stdout); }
inline bool tty() {
#ifdef _WIN32
    return _isatty(_fileno(stdin));
#else
    return isatty(0);
#endif
}
inline int cols() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO i; if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &i)) return i.srWindow.Right - i.srWindow.Left + 1; return 100;
#else
    winsize w{}; if (ioctl(1, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) return w.ws_col; return 100;
#endif
}
inline int rows() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO i; if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &i)) return i.srWindow.Bottom - i.srWindow.Top + 1; return 30;
#else
    winsize w{}; if (ioctl(1, TIOCGWINSZ, &w) == 0 && w.ws_row > 0) return w.ws_row; return 30;
#endif
}
#ifndef _WIN32
inline termios& saved() { static termios t; return t; }
inline bool& raw() { static bool r = false; return r; }
#endif
inline void shutdown() {
    if (done()) return; done() = true;
    out("\x1b[0m\x1b[?25h\x1b[?1049l");
#ifndef _WIN32
    if (raw()) tcsetattr(0, TCSADRAIN, &saved());
#endif
}
#ifndef _WIN32
inline void onSignal(int) { shutdown(); _exit(130); }
#endif
inline void init() {
#ifdef _WIN32
    SetConsoleOutputCP(65001); HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE); DWORD m = 0; GetConsoleMode(h, &m); SetConsoleMode(h, m | 0x0004);
#else
    if (tty()) {
        tcgetattr(0, &saved()); termios t = saved();
        t.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN); t.c_iflag &= ~(IXON | ICRNL | INLCR | ISTRIP); t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
        tcsetattr(0, TCSADRAIN, &t); raw() = true;
        signal(SIGTERM, onSignal); signal(SIGHUP, onSignal); signal(SIGINT, onSignal);
    }
#endif
    out("\x1b[?1049h\x1b[?25l\x1b[2J"); atexit(shutdown);
}
#ifndef _WIN32
inline int rb(int ms) {
    fd_set f; FD_ZERO(&f); FD_SET(0, &f); timeval tv{ms / 1000, (ms % 1000) * 1000};
    int r = select(1, &f, nullptr, nullptr, ms < 0 ? nullptr : &tv); if (r <= 0) return -1;
    unsigned char ch; ssize_t n = read(0, &ch, 1); if (n == 0) { shutdown(); std::exit(0); } if (n < 0) return -1; return ch;
}
#endif
inline Key readKey() {
    auto start = std::chrono::steady_clock::now();
    auto idleCheck = [&]() { if (idleLimit() > 0 && std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count() >= idleLimit()) throw SessionTimeout{}; };
#ifdef _WIN32
    while (!_kbhit()) { Sleep(30); idleCheck(); }
    int ch = _getwch();
    if (ch == 0 || ch == 0xE0) { int c2 = _getwch(); switch (c2) { case 72: return {K::Up, ""}; case 80: return {K::Down, ""}; case 75: return {K::Left, ""}; case 77: return {K::Right, ""}; case 71: return {K::Home, ""}; case 79: return {K::End, ""}; case 73: return {K::PgUp, ""}; case 81: return {K::PgDn, ""}; case 83: return {K::Del, ""}; } return {K::Esc, ""}; }
    if (ch == 13) return {K::Enter, ""}; if (ch == 27) return {K::Esc, ""}; if (ch == 8) return {K::Back, ""}; if (ch == 9) return {K::Tab, ""};
    if (ch == 3) { shutdown(); std::exit(0); }
    std::string t; if (ch < 0x80) t += (char)ch; else if (ch < 0x800) { t += (char)(0xC0 | (ch >> 6)); t += (char)(0x80 | (ch & 0x3F)); } else { t += (char)(0xE0 | (ch >> 12)); t += (char)(0x80 | ((ch >> 6) & 0x3F)); t += (char)(0x80 | (ch & 0x3F)); }
    return {K::Char, t};
#else
    int ch; while (true) { ch = rb(500); if (ch >= 0) break; idleCheck(); }
    if (ch == 27) {
        int n = rb(30); if (n < 0) return {K::Esc, ""};
        if (n == '[' || n == 'O') {
            std::string seq; int x; while ((x = rb(30)) >= 0) { seq += (char)x; if ((x >= '@' && x <= '~')) break; }
            if (seq == "A") return {K::Up, ""}; if (seq == "B") return {K::Down, ""}; if (seq == "C") return {K::Right, ""}; if (seq == "D") return {K::Left, ""};
            if (seq == "H" || seq == "1~" || seq == "7~") return {K::Home, ""}; if (seq == "F" || seq == "4~" || seq == "8~") return {K::End, ""};
            if (seq == "3~") return {K::Del, ""}; if (seq == "5~") return {K::PgUp, ""}; if (seq == "6~") return {K::PgDn, ""};
            return {K::Esc, ""};
        }
        return {K::Esc, ""};
    }
    if (ch == 13 || ch == 10) return {K::Enter, ""}; if (ch == 127 || ch == 8) return {K::Back, ""}; if (ch == 9) return {K::Tab, ""};
    if (ch == 3 || ch == 4) { shutdown(); std::exit(0); }
    if (ch < 32) return readKey();
    std::string t(1, (char)ch); int more = ch >= 0xF0 ? 3 : ch >= 0xE0 ? 2 : ch >= 0xC0 ? 1 : 0;
    while (more-- > 0) { int x = rb(30); if (x < 0) break; t += (char)x; }
    return {K::Char, t};
#endif
}
} // namespace term

// ---------- screen composition ----------
struct Ctx { std::string company = "CORP-CONTROL++", user, role; int unread = 0; bool maint = false; std::vector<std::string> crumbs; };
inline Ctx& ctx() { static Ctx x; return x; }
struct Crumb { explicit Crumb(const std::string& s) { ctx().crumbs.push_back(s); } ~Crumb() { ctx().crumbs.pop_back(); } };
inline int W() { return std::max(70, std::min(term::cols() - 4, 118)); }

inline void draw(const std::string& body) {
    std::string o = "\x1b[H"; o.reserve(body.size() + 256);
    for (char ch : body) { if (ch == '\n') o += "\x1b[K\n"; else o += ch; }
    o += "\x1b[K\x1b[J"; term::out(o);
}
inline std::string boxTop(int w, const std::string& title, const char* col) {
    std::string t = title.empty() ? "" : " " + title + " ";
    return "  " + paint(col, "╭─") + (title.empty() ? "" : paint(c::BOLD, t)) + paint(col, rep("─", std::max(0, w - 3 - vlen(t))) + "╮") + "\n";
}
inline std::string boxLine(const std::string& s, int w, const char* col) { return "  " + paint(col, "│") + " " + padR(s, w - 4) + " " + paint(col, "│") + "\n"; }
inline std::string boxBot(int w, const char* col) { return "  " + paint(col, "╰" + rep("─", w - 2) + "╯") + "\n"; }
inline std::string box(const std::vector<std::string>& lines, int w, const std::string& title = "", const char* col = c::MUTE) {
    std::string o = boxTop(w, title, col); for (auto& l : lines) o += boxLine(l, w, col); return o + boxBot(w, col);
}
inline std::string heading(const std::string& t) { return "  " + paint(c::ACC, c::BOLD + t) + "\n  " + paint(c::MUTE, rep("─", std::max(8, vlen(t)))) + "\n"; }

inline std::string banner() {
    int w = W(); auto& x = ctx();
    std::string l1 = paint(c::ACC, "◆ ") + paint(c::BOLD, "CORP-CONTROL++") + paint(c::MUTE, "  │  ") + x.company;
    std::string r1 = x.maint ? paint(c::WARN, "● MAINTENANCE") : paint(c::OK, "● ONLINE");
    std::string l2 = x.user.empty() ? paint(c::MUTE, "not signed in") : paint(c::BOLD, x.user) + paint(c::MUTE, "  ·  ") + paint(c::ACC2, x.role);
    std::string r2 = (x.unread > 0 ? paint(c::WARN, "✉ " + std::to_string(x.unread) + "  ") : "") + paint(c::MUTE, util::fmtNow());
    auto row = [&](const std::string& l, const std::string& r) { int gap = std::max(1, w - 4 - vlen(l) - vlen(r)); return l + rep(" ", gap) + r; };
    std::string o = "\n" + box({row(l1, r1), row(l2, r2)}, w, "", c::ACC2);
    std::string cr = "  " + paint(c::MUTE, "⌂ Home"); for (auto& s : x.crumbs) cr += paint(c::MUTE, " › ") + s;
    return o + cr + "\n\n";
}

inline void screen(const std::string& body, const std::string& footer = "") {
    std::string o = banner() + body; if (!footer.empty()) o += "\n  " + paint(c::MUTE, footer) + "\n"; draw(o);
}

// ---------- badges, tiles ----------
inline std::string badge(const std::string& s) {
    std::string l = util::lower(s); const char* col = c::ACC;
    if (l == "approved" || l == "paid" || l == "active" || l == "available" || l == "present" || l == "generated" || l == "provisioned") col = c::OK;
    else if (l.rfind("pending", 0) == 0 || l == "repair" || l == "on leave" || l == "locked") col = c::WARN;
    else if (l == "rejected" || l == "cancelled" || l == "exited" || l == "retired" || l == "disabled" || l == "absent") col = c::BAD;
    return paint(col, "● " + s);
}
struct Tile { std::string label, value; const char* color; };
inline std::string tiles(const std::vector<Tile>& t) {
    int n = (int)t.size(); if (!n) return ""; int tw = (W() - (n - 1) * 2) / n; std::string l[4];
    for (int i = 0; i < n; i++) {
        std::string gap = i ? "  " : "";
        l[0] += gap + paint(c::MUTE, "╭" + rep("─", tw - 2) + "╮");
        l[1] += gap + paint(c::MUTE, "│") + " " + padR(paint(t[i].color, std::string(c::BOLD) + t[i].value), tw - 4) + " " + paint(c::MUTE, "│");
        l[2] += gap + paint(c::MUTE, "│") + " " + padR(paint(c::MUTE, t[i].label), tw - 4) + " " + paint(c::MUTE, "│");
        l[3] += gap + paint(c::MUTE, "╰" + rep("─", tw - 2) + "╯");
    }
    return "  " + l[0] + "\n  " + l[1] + "\n  " + l[2] + "\n  " + l[3] + "\n";
}

// ---------- menu ----------
struct MenuItem { std::string label, hint; bool enabled = true; std::string badge; };
inline int menu(const std::string& ttl, const std::vector<MenuItem>& items, const std::string& pre = "", int start = 0) {
    int n = (int)items.size(), sel = std::min(std::max(start, 0), std::max(0, n - 1)); int w = W();
    auto nextEnabled = [&](int from, int dir) { for (int i = 0; i < n; i++) { from = (from + dir + n) % n; if (items[from].enabled) return from; } return from; };
    if (n && !items[sel].enabled) sel = nextEnabled(sel, 1);
    while (true) {
        std::string b = pre; if (!ttl.empty()) b += heading(ttl); b += "\n";
        for (int i = 0; i < n; i++) {
            std::string num = i < 9 ? std::to_string(i + 1) : "·";
            std::string label = padR(items[i].label, 30);
            if (i == sel) b += "  " + std::string(c::SEL) + padR(" ▸ " + num + "  " + label + " " + strip(items[i].hint) + (items[i].badge.empty() ? "" : "  " + strip(items[i].badge)), w) + c::R + "\n";
            else if (!items[i].enabled) b += "  " + paint(c::DIM, "   " + num + "  " + label + " " + items[i].hint) + "\n";
            else b += "    " + paint(c::MUTE, num) + "  " + label + " " + paint(c::MUTE, items[i].hint) + (items[i].badge.empty() ? "" : "  " + items[i].badge) + "\n";
        }
        screen(b, "↑↓ navigate   ⏎ select   1-9 jump   Esc back");
        Key k = term::readKey();
        if (k.k == K::Up) sel = nextEnabled(sel, -1); else if (k.k == K::Down) sel = nextEnabled(sel, 1);
        else if (k.k == K::Enter && n) return sel; else if (k.k == K::Esc) return -1;
        else if (k.k == K::Home) sel = nextEnabled(-1, 1); else if (k.k == K::End) sel = nextEnabled(0, -1);
        else if (k.k == K::Char && k.text.size() == 1 && k.text[0] >= '1' && k.text[0] <= '9') { int i = k.text[0] - '1'; if (i < n && items[i].enabled) return i; }
    }
}

// ---------- tables ----------
struct Col { std::string name; int maxw = 36; bool right = false; };
struct Pick { int idx = -1; int key = 0; };
struct PickOpt { std::string title, pre; std::vector<std::pair<char, std::string>> hot; bool selectable = true; std::string empty = "Nothing here yet."; int start = 0; std::string note; };
inline Pick pickTable(const std::vector<Col>& cols, const std::vector<std::vector<std::string>>& rows, const PickOpt& o) {
    int nc = (int)cols.size(), n = (int)rows.size(), w = W(); std::vector<int> cw(nc);
    for (int i = 0; i < nc; i++) { int m = vlen(cols[i].name); for (auto& r : rows) m = std::max(m, vlen(r[i])); cw[i] = std::min(m, cols[i].maxw); }
    auto total = [&]() { int t = 1; for (int x : cw) t += x + 3; return t; };
    while (total() > w) { int bi = 0; for (int i = 1; i < nc; i++) if (cw[i] > cw[bi]) bi = i; if (cw[bi] <= 5) break; cw[bi]--; }
    int sel = std::min(std::max(o.start, 0), std::max(0, n - 1)), from = 0;
    int preLines = (int)std::count(o.pre.begin(), o.pre.end(), '\n');
    std::string foot = std::string(o.selectable && n ? "↑↓ move   ⏎ open   " : "↑↓ scroll   ");
    for (auto& h : o.hot) foot += std::string("[") + (char)toupper(h.first) + "] " + h.second + "   "; foot += "Esc back";
    while (true) {
        int vis = std::max(5, term::rows() - 19 - preLines); if (sel < from) from = sel; if (sel >= from + vis) from = sel - vis + 1;
        std::string b = o.pre; if (!o.title.empty()) b += heading(o.title);
        if (!o.note.empty()) b += "  " + paint(c::MUTE, o.note) + "\n";
        if (n == 0) { b += "\n" + box({"", paint(c::MUTE, o.empty), ""}, std::min(w, 60)); }
        else {
            std::string top = "  ", hd = "  ", sp = "  ", bt = "  ";
            for (int i = 0; i < nc; i++) {
                std::string seg = rep("─", cw[i] + 2); top += paint(c::MUTE, i ? "┬" + seg : "╭" + seg); sp += paint(c::MUTE, i ? "┼" + seg : "├" + seg); bt += paint(c::MUTE, i ? "┴" + seg : "╰" + seg);
                hd += paint(c::MUTE, "│") + " " + paint(c::ACC, std::string(c::BOLD) + (cols[i].right ? padL(cols[i].name, cw[i]) : padR(cols[i].name, cw[i]))) + " ";
            }
            top += paint(c::MUTE, "╮"); sp += paint(c::MUTE, "┤"); bt += paint(c::MUTE, "╯"); hd += paint(c::MUTE, "│");
            b += top + "\n" + hd + "\n" + sp + "\n";
            for (int r = from; r < std::min(n, from + vis); r++) {
                bool s = o.selectable && r == sel; std::string ln = "  ";
                if (s) {
                    std::string inner;
                    for (int i = 0; i < nc; i++) { std::string cell = strip(rows[r][i]); inner += std::string(i ? " │ " : " ") + (cols[i].right ? padL(cell, cw[i]) : padR(cell, cw[i])); }
                    ln += paint(c::MUTE, "│") + c::SEL + inner + " " + c::R + paint(c::MUTE, "│");
                } else {
                    for (int i = 0; i < nc; i++) ln += paint(c::MUTE, "│") + " " + (cols[i].right ? padL(rows[r][i], cw[i]) : padR(rows[r][i], cw[i])) + " ";
                    ln += paint(c::MUTE, "│");
                }
                b += ln + "\n";
            }
            b += bt + "\n  " + paint(c::MUTE, "Showing " + std::to_string(from + 1) + "–" + std::to_string(std::min(n, from + vis)) + " of " + std::to_string(n)) + "\n";
        }
        screen(b, foot);
        Key k = term::readKey();
        if (k.k == K::Esc) return {-1, 0};
        if (k.k == K::Up && n) sel = std::max(0, sel - 1); else if (k.k == K::Down && n) sel = std::min(n - 1, sel + 1);
        else if (k.k == K::PgUp && n) sel = std::max(0, sel - vis); else if (k.k == K::PgDn && n) sel = std::min(n - 1, sel + vis);
        else if (k.k == K::Home && n) sel = 0; else if (k.k == K::End && n) sel = n - 1;
        else if (k.k == K::Enter && o.selectable && n) return {sel, 0};
        else if (k.k == K::Char) for (auto& h : o.hot) if (k.is(h.first)) return {n ? sel : -1, tolower(h.first)};
    }
}

// ---------- message boxes ----------
inline std::vector<std::string> wrapLines(const std::string& t, int w) { return wrap(t, w - 6); }
inline bool confirm(const std::string& msg, bool defYes = false, const char* col = c::WARN) {
    int w = std::min(W(), 76); std::vector<std::string> l{""}; for (auto& s : wrapLines(msg, w)) l.push_back(s); l.push_back("");
    while (true) {
        screen("\n" + box(l, w, "Confirm", col), std::string(defYes ? "⏎/Y yes   N no" : "Y yes   ⏎/N no") + "   Esc cancel");
        Key k = term::readKey(); if (k.is('y')) return true; if (k.is('n') || k.k == K::Esc) return false; if (k.k == K::Enter) return defYes;
    }
}
inline void flash(char kind, const std::string& msg, const std::string& ttl = "") {
    const char* col = kind == 'o' ? c::OK : kind == 'e' ? c::BAD : kind == 'w' ? c::WARN : c::ACC;
    std::string ico = kind == 'o' ? "✔ " : kind == 'e' ? "✖ " : kind == 'w' ? "! " : "ℹ ";
    int w = std::min(W(), 84); std::vector<std::string> l{""}; auto ws = wrapLines(msg, w); for (size_t i = 0; i < ws.size(); i++) l.push_back((i == 0 ? paint(col, ico) : "  ") + ws[i]); l.push_back("");
    screen("\n" + box(l, w, ttl.empty() ? (kind == 'o' ? "Success" : kind == 'e' ? "Error" : kind == 'w' ? "Notice" : "Info") : ttl, col), "Press any key to continue");
    term::readKey();
}
inline int card(const std::string& ttl, const std::vector<std::pair<std::string, std::string>>& kv, const std::vector<std::pair<char, std::string>>& hot = {}, const std::string& pre = "") {
    int w = W(); std::vector<std::string> l{""};
    for (auto& p : kv) { auto ws = wrap(p.second, w - 26); if (ws.empty()) ws.push_back(""); for (size_t i = 0; i < ws.size(); i++) l.push_back(paint(c::MUTE, padR(i ? "" : p.first, 18)) + "  " + ws[i]); }
    l.push_back(""); std::string foot; for (auto& h : hot) foot += std::string("[") + (char)toupper(h.first) + "] " + h.second + "   "; foot += "Esc back";
    while (true) {
        screen(pre + "\n" + box(l, w, ttl, c::ACC), foot); Key k = term::readKey();
        if (k.k == K::Esc) return 0; if (k.k == K::Char) for (auto& h : hot) if (k.is(h.first)) return tolower(h.first);
    }
}
inline void textView(const std::string& ttl, const std::vector<std::string>& lines) {
    int w = W(), pos = 0;
    while (true) {
        int vis = std::max(5, term::rows() - 16); pos = std::max(0, std::min(pos, std::max(0, (int)lines.size() - vis)));
        std::vector<std::string> v; for (int i = pos; i < std::min((int)lines.size(), pos + vis); i++) v.push_back(lines[i]);
        screen("\n" + box(v, w, ttl), "↑↓ scroll   Esc back"); Key k = term::readKey();
        if (k.k == K::Esc || k.k == K::Enter) return; if (k.k == K::Up) pos--; if (k.k == K::Down) pos++; if (k.k == K::PgUp) pos -= vis; if (k.k == K::PgDn) pos += vis;
    }
}

// ---------- forms ----------
struct FormOpt { std::string hint, def; bool mask = false, optional = false; std::function<std::string(const std::string&)> check; size_t maxLen = 120; };
class Form {
public:
    using Opt = FormOpt;
    explicit Form(std::string title, std::string pre = "") : title_(std::move(title)), pre_(std::move(pre)) {}
    bool ask(const std::string& label, std::string& out, const Opt& o = Opt()) {
        std::string buf = o.def, err;
        while (true) {
            std::string shown = o.mask ? rep("•", (int)buf.size()) : buf;
            std::string cur = "  " + paint(c::ACC, "▸ ") + padR(paint(c::BOLD, label), 24) + " " + shown + paint(c::ACC, "█");
            std::string extra = cur + "\n"; if (!o.hint.empty()) extra += "      " + paint(c::MUTE, o.hint) + "\n"; if (!err.empty()) extra += "      " + paint(c::BAD, "✖ " + err) + "\n";
            screen(render() + extra, "⏎ confirm   Esc cancel" + std::string(o.optional ? "   (optional - leave blank to skip)" : ""));
            Key k = term::readKey();
            if (k.k == K::Esc) return false;
            if (k.k == K::Back) { while (!buf.empty() && (buf.back() & 0xC0) == 0x80) buf.pop_back(); if (!buf.empty()) buf.pop_back(); err.clear(); }
            else if (k.k == K::Char && buf.size() < o.maxLen) { buf += k.text; err.clear(); }
            else if (k.k == K::Enter) {
                std::string v = util::trim(buf);
                if (v.empty() && !o.optional) { err = "This field is required"; continue; }
                if (!v.empty() && o.check) { err = o.check(v); if (!err.empty()) continue; }
                out = v; done_.push_back({label, o.mask ? rep("•", 8) : (v.empty() ? paint(c::MUTE, "—") : v)}); return true;
            }
        }
    }
    bool choose(const std::string& label, const std::vector<std::string>& ch, int& idx, int def = 0) {
        int sel = std::min(std::max(def, 0), std::max(0, (int)ch.size() - 1)), from = 0;
        while (true) {
            int vis = std::max(4, term::rows() - 20 - (int)done_.size()); if (sel < from) from = sel; if (sel >= from + vis) from = sel - vis + 1;
            std::string b = render() + "  " + paint(c::ACC, "▸ ") + paint(c::BOLD, label) + "\n";
            for (int i = from; i < std::min((int)ch.size(), from + vis); i++) b += i == sel ? "      " + std::string(c::SEL) + padR(" ● " + strip(ch[i]), 44) + c::R + "\n" : "      " + paint(c::MUTE, "○ ") + ch[i] + "\n";
            screen(b, "↑↓ choose   ⏎ select   Esc cancel"); Key k = term::readKey();
            if (k.k == K::Esc) return false; if (k.k == K::Up) sel = (sel + (int)ch.size() - 1) % (int)ch.size(); if (k.k == K::Down) sel = (sel + 1) % (int)ch.size();
            if (k.k == K::Enter) { idx = sel; done_.push_back({label, ch[sel]}); return true; }
        }
    }
private:
    std::string render() { std::string b = pre_ + heading(title_); for (auto& d : done_) b += "  " + paint(c::OK, "✔ ") + paint(c::MUTE, padR(d.first, 24)) + " " + d.second + "\n"; return b; }
    std::string title_, pre_; std::vector<std::pair<std::string, std::string>> done_;
};
} // namespace ui
