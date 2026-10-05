// util.hpp - strings, dates, money, hashing (zero dependencies)
#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <optional>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <ctime>
#include <random>
#include <sstream>

namespace util {

inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) a++;
    while (b > a && isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}
inline std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
inline bool icontains(const std::string& h, const std::string& n) { return lower(h).find(lower(n)) != std::string::npos; }
inline std::vector<std::string> split(const std::string& s, char d) {
    std::vector<std::string> v; std::string cur;
    for (char c : s) { if (c == d) { v.push_back(cur); cur.clear(); } else cur += c; }
    v.push_back(cur); return v;
}
inline std::string rep(const std::string& s, int n) { std::string o; for (int i = 0; i < n; i++) o += s; return o; }
inline std::string pad2(int n) { char b[8]; snprintf(b, sizeof b, "%02d", n); return b; }

// ---------- time ----------
inline int64_t now() { return (int64_t)time(nullptr); }
inline std::tm localtm(int64_t t) {
    std::tm tm{}; time_t tt = (time_t)t;
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    return tm;
}
inline std::string fmtTs(int64_t t) { if (t <= 0) return "-"; char b[32]; auto tm = localtm(t); strftime(b, sizeof b, "%Y-%m-%d %H:%M", &tm); return b; }
inline std::string fmtClock(int64_t t) { if (t <= 0) return "-"; char b[16]; auto tm = localtm(t); strftime(b, sizeof b, "%H:%M", &tm); return b; }
inline std::string fmtNow() { char b[48]; auto tm = localtm(now()); strftime(b, sizeof b, "%d %b %Y  %H:%M", &tm); return b; }
inline std::string fmtStamp() { char b[32]; auto tm = localtm(now()); strftime(b, sizeof b, "%Y%m%d-%H%M%S", &tm); return b; }

inline int64_t daysFromCivil(int y, int m, int d) {
    y -= m <= 2; int64_t era = (y >= 0 ? y : y - 399) / 400; unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}
inline void civilFromDays(int64_t z, int& y, int& m, int& d) {
    z += 719468; int64_t era = (z >= 0 ? z : z - 146096) / 146097; unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; y = (int)(yoe + era * 400);
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100); unsigned mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1); m = (int)(mp < 10 ? mp + 3 : mp - 9); y += m <= 2;
}
inline int64_t todayDays() { auto tm = localtm(now()); return daysFromCivil(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday); }
inline std::string fmtDate(int64_t days) { int y, m, d; civilFromDays(days, y, m, d); char b[32]; snprintf(b, sizeof b, "%04d-%02d-%02d", y, m, d); return b; }
inline std::string niceDate(int64_t days) {
    static const char* M[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    int y, m, d; civilFromDays(days, y, m, d); char b[24]; snprintf(b, sizeof b, "%02d %s %d", d, M[m - 1], y); return b;
}
inline std::string monthOf(int64_t days) { int y, m, d; civilFromDays(days, y, m, d); char b[12]; snprintf(b, sizeof b, "%04d-%02d", y, m); return b; }
inline int weekday(int64_t days) { return (int)((((days + 4) % 7) + 7) % 7); } // 0 = Sunday
inline bool validYMD(int y, int m, int d) {
    if (y < 1900 || y > 2200 || m < 1 || m > 12 || d < 1) return false;
    static const int dm[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int md = dm[m - 1]; if (m == 2 && ((y % 4 == 0 && y % 100) || y % 400 == 0)) md = 29;
    return d <= md;
}
inline bool parseDate(std::string s, int64_t& out) {
    s = lower(trim(s));
    if (s == "today" || s == "t") { out = todayDays(); return true; }
    int a, b, c, n = 0; char s1, s2;
    if (sscanf(s.c_str(), "%d%c%d%c%d%n", &a, &s1, &b, &s2, &c, &n) != 5 || n != (int)s.size()) return false;
    if (!strchr("-/.", s1) || s1 != s2) return false;
    int y, m, d; if (a > 31) { y = a; m = b; d = c; } else { d = a; m = b; y = c; }
    if (!validYMD(y, m, d)) return false;
    out = daysFromCivil(y, m, d); return true;
}
inline bool monthRange(const std::string& ym, int64_t& a, int64_t& b) { // "YYYY-MM"
    int y, m; if (sscanf(ym.c_str(), "%d-%d", &y, &m) != 2 || !validYMD(y, m, 1)) return false;
    a = daysFromCivil(y, m, 1); int ny = m == 12 ? y + 1 : y, nm = m == 12 ? 1 : m + 1; b = daysFromCivil(ny, nm, 1) - 1; return true;
}

// ---------- money (stored as integer paise) ----------
inline std::string groupIndian(long long n) {
    std::string s = std::to_string(n < 0 ? -n : n);
    if (s.size() > 3) {
        std::string last = s.substr(s.size() - 3), rest = s.substr(0, s.size() - 3), o;
        int cnt = 0; for (int i = (int)rest.size() - 1; i >= 0; i--) { o += rest[i]; if (++cnt % 2 == 0 && i > 0) o += ','; }
        std::reverse(o.begin(), o.end()); s = o + "," + last;
    }
    return (n < 0 ? "-" : "") + s;
}
inline std::string money(long long paise, const std::string& cur) {
    long long a = paise < 0 ? -paise : paise; char f[8]; snprintf(f, sizeof f, ".%02lld", a % 100);
    return cur + groupIndian((paise < 0 ? -1 : 1) * (a / 100)) + f;
}
inline bool parseMoney(std::string s, long long& paise) {
    s.erase(std::remove(s.begin(), s.end(), ','), s.end()); s = trim(s);
    if (s.empty()) return false;
    size_t dot = s.find('.'); std::string ip = dot == std::string::npos ? s : s.substr(0, dot), fp = dot == std::string::npos ? "" : s.substr(dot + 1);
    if (ip.empty() || ip.size() > 12 || fp.size() > 2) return false;
    for (char c : ip + fp) if (!isdigit((unsigned char)c)) return false;
    while (fp.size() < 2) fp += '0';
    paise = std::stoll(ip) * 100 + std::stoll(fp); return true;
}
inline bool parseInt(const std::string& s, long long& v) {
    std::string t = trim(s); if (t.empty() || t.size() > 15) return false; size_t i = (t[0] == '-') ? 1 : 0; if (i == t.size()) return false;
    for (; i < t.size(); i++) if (!isdigit((unsigned char)t[i])) return false;
    v = std::stoll(t); return true;
}

// ---------- hashing ----------
inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline std::string sha256raw(const std::string& msg) {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::string m = msg; uint64_t bits = (uint64_t)msg.size() * 8; m.push_back((char)0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 7; i >= 0; i--) m.push_back((char)((bits >> (i * 8)) & 0xff));
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) w[i] = ((uint32_t)(uint8_t)m[off + 4*i] << 24) | ((uint32_t)(uint8_t)m[off + 4*i + 1] << 16) | ((uint32_t)(uint8_t)m[off + 4*i + 2] << 8) | (uint32_t)(uint8_t)m[off + 4*i + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15] >> 3), s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25), ch = (e & f) ^ (~e & g), t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22), mj = (a & b) ^ (a & c) ^ (b & c), t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
    }
    std::string out; for (int i = 0; i < 8; i++) for (int j = 3; j >= 0; j--) out.push_back((char)((h[i] >> (j * 8)) & 0xff));
    return out;
}
inline std::string toHex(const std::string& s) { static const char* H = "0123456789abcdef"; std::string o; for (unsigned char c : s) { o += H[c >> 4]; o += H[c & 15]; } return o; }
inline std::string sha256hex(const std::string& s) { return toHex(sha256raw(s)); }
inline std::string randomHex(size_t bytes) { std::random_device rd; std::string s; for (size_t i = 0; i < bytes; i++) s.push_back((char)(rd() & 0xff)); return toHex(s); }
inline std::string hashPassword(const std::string& pw, const std::string& salt) {
    std::string h = sha256raw(salt + pw);
    for (int i = 0; i < 60000; i++) h = sha256raw(h + salt + pw);   // iterated, salted
    return toHex(h);
}
inline bool ctEq(const std::string& a, const std::string& b) { if (a.size() != b.size()) return false; unsigned char d = 0; for (size_t i = 0; i < a.size(); i++) d |= (unsigned char)(a[i] ^ b[i]); return d == 0; }
inline uint32_t crc32(const std::string& s) {
    static uint32_t T[256]; static bool init = false;
    if (!init) { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; T[i] = c; } init = true; }
    uint32_t c = 0xFFFFFFFFu; for (unsigned char ch : s) c = T[(c ^ ch) & 0xff] ^ (c >> 8); return c ^ 0xFFFFFFFFu;
}
inline std::string tempPassword() {
    static const char* A = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789"; std::random_device rd; std::string s;
    for (int i = 0; i < 10; i++) s += A[rd() % strlen(A)];
    s += (char)('2' + rd() % 8); return s;
}
inline std::string csvEscape(const std::string& s) {
    if (s.find_first_of(",\"\n\r") == std::string::npos) return s;
    std::string o = "\""; for (char c : s) { if (c == '"') o += "\"\""; else o += c; } return o + "\"";
}
} // namespace util
