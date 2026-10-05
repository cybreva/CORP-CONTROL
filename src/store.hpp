// store.hpp - embedded, crash-safe, multi-process storage engine
//  * every table = append-only log (CRC32 per line, fsync on write)
//  * a global file lock serialises writers; readers tail-follow the log, so
//    several terminals (SSH sessions) can share one data directory safely
//  * audit trail is a SHA-256 hash chain (tamper evident)
#pragma once
#include "util.hpp"
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#ifdef _WIN32
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #include <windows.h>
  #include <io.h>
#else
  #include <sys/file.h>
  #include <unistd.h>
  #include <fcntl.h>
#endif

namespace store {
namespace fs = std::filesystem;
using Fields = std::map<std::string, std::string>;

struct Rec {
    uint64_t id = 0; Fields f;
    std::string s(const std::string& k, const std::string& d = "") const { auto it = f.find(k); return it == f.end() ? d : it->second; }
    long long n(const std::string& k, long long d = 0) const { auto it = f.find(k); if (it == f.end() || it->second.empty()) return d; try { return std::stoll(it->second); } catch (...) { return d; } }
    bool b(const std::string& k) const { return n(k) != 0; }
    Rec& set(const std::string& k, const std::string& v) { f[k] = v; return *this; }
    Rec& set(const std::string& k, long long v) { f[k] = std::to_string(v); return *this; }
};

inline std::string esc(const std::string& s) {
    std::string o; for (char c : s) { switch (c) { case '\\': o += "\\\\"; break; case '\n': o += "\\n"; break; case '\r': o += "\\r"; break; case '\t': o += "\\t"; break; case '=': o += "\\e"; break; default: o += c; } } return o;
}
inline std::string unesc(const std::string& s) {
    std::string o; for (size_t i = 0; i < s.size(); i++) { if (s[i] == '\\' && i + 1 < s.size()) { char n = s[++i]; o += n == 'n' ? '\n' : n == 'r' ? '\r' : n == 't' ? '\t' : n == 'e' ? '=' : n; } else o += s[i]; } return o;
}

// Process-wide, re-entrant exclusive lock on <data>/.lock
class DbLock {
public:
    static void setPath(const std::string& p) { path() = p; }
    DbLock() { if (depth()++ == 0) acquire(); }
    ~DbLock() { if (--depth() == 0) release(); }
    DbLock(const DbLock&) = delete; DbLock& operator=(const DbLock&) = delete;
private:
    static std::string& path() { static std::string p; return p; }
    static int& depth() { static int d = 0; return d; }
#ifdef _WIN32
    static HANDLE& h() { static HANDLE x = INVALID_HANDLE_VALUE; return x; }
    static void acquire() { h() = CreateFileA(path().c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr); OVERLAPPED ov{}; LockFileEx(h(), LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &ov); }
    static void release() { OVERLAPPED ov{}; UnlockFileEx(h(), 0, 1, 0, &ov); CloseHandle(h()); h() = INVALID_HANDLE_VALUE; }
#else
    static int& fd() { static int x = -1; return x; }
    static void acquire() { fd() = ::open(path().c_str(), O_CREAT | O_RDWR, 0600); if (fd() >= 0) flock(fd(), LOCK_EX); }
    static void release() { if (fd() >= 0) { flock(fd(), LOCK_UN); ::close(fd()); fd() = -1; } }
#endif
};

inline void syncFile(FILE* fp) {
    fflush(fp);
#ifdef _WIN32
    _commit(_fileno(fp));
#else
    fsync(fileno(fp));
#endif
}

class Table {
public:
    Table(std::string name, std::string path) : name_(std::move(name)), path_(std::move(path)) {}

    std::vector<Rec> all() { sync(); std::vector<Rec> v; v.reserve(rows_.size()); for (auto& kv : rows_) v.push_back(kv.second); return v; }
    std::optional<Rec> get(uint64_t id) { sync(); auto it = rows_.find(id); if (it == rows_.end()) return std::nullopt; return it->second; }
    std::vector<Rec> where(const std::function<bool(const Rec&)>& p) { sync(); std::vector<Rec> v; for (auto& kv : rows_) if (p(kv.second)) v.push_back(kv.second); return v; }
    std::optional<Rec> first(const std::function<bool(const Rec&)>& p) { sync(); for (auto& kv : rows_) if (p(kv.second)) return kv.second; return std::nullopt; }
    size_t count() { sync(); return rows_.size(); }
    size_t badLines() { sync(); return bad_; }

    uint64_t insert(Fields f) { DbLock L; sync(); uint64_t id = next_; append('I', id, f); sync(); return id; }
    // returns 0 when a record with the same values for all `keys` already exists
    uint64_t insertUnique(Fields f, const std::vector<std::string>& keys) {
        DbLock L; sync();
        for (auto& kv : rows_) { bool same = true; for (auto& k : keys) if (kv.second.s(k) != (f.count(k) ? f[k] : "")) { same = false; break; } if (same) return 0; }
        uint64_t id = next_; append('I', id, f); sync(); return id;
    }
    // atomic read-modify-write; fn returns false to abort
    bool modify(uint64_t id, const std::function<bool(Rec&)>& fn) {
        DbLock L; sync(); auto it = rows_.find(id); if (it == rows_.end()) return false;
        Rec r = it->second; if (!fn(r)) return false; append('U', id, r.f); sync(); return true;
    }
    bool remove(uint64_t id) { DbLock L; sync(); if (!rows_.count(id)) return false; append('D', id, {}); sync(); return true; }

    void compact() {
        DbLock L; sync(); std::string tmp = path_ + ".tmp"; FILE* fp = fopen(tmp.c_str(), "wb"); if (!fp) return;
        std::string hdr = "#CC1 gen=" + util::randomHex(6) + "\n"; fwrite(hdr.data(), 1, hdr.size(), fp);
        for (auto& kv : rows_) { std::string ln = makeLine('I', kv.first, kv.second.f); fwrite(ln.data(), 1, ln.size(), fp); }
        syncFile(fp); fclose(fp); fs::rename(tmp, path_); loaded_ = false; sync();
    }
    uintmax_t fileSize() { std::error_code ec; auto s = fs::file_size(path_, ec); return ec ? 0 : s; }
    const std::string& name() const { return name_; }

private:
    static std::string makeLine(char op, uint64_t id, const Fields& f) {
        std::string body; body += op; body += '\t'; body += std::to_string(id);
        for (auto& kv : f) { body += '\t'; body += esc(kv.first); body += '='; body += esc(kv.second); }
        char crc[16]; snprintf(crc, sizeof crc, "%08x", util::crc32(body));
        return std::string(crc) + "\t" + body + "\n";
    }
    void append(char op, uint64_t id, const Fields& f) {
        std::error_code ec; std::string out;
        bool fresh = !fs::exists(path_, ec) || fs::file_size(path_, ec) == 0;
        if (fresh) out += "#CC1 gen=" + util::randomHex(6) + "\n";
        else { std::ifstream in(path_, std::ios::binary | std::ios::ate); if (in.tellg() > 0) { in.seekg(-1, std::ios::end); char c; in.get(c); if (c != '\n') out += "\n"; } }
        out += makeLine(op, id, f);
        FILE* fp = fopen(path_.c_str(), "ab"); if (!fp) throw std::runtime_error("cannot write " + path_);
        fwrite(out.data(), 1, out.size(), fp); syncFile(fp); fclose(fp);
    }
    void apply(const std::string& line) {
        if (line.empty() || line[0] == '#') return;
        if (line.size() < 10 || line[8] != '\t') { bad_++; return; }
        std::string body = line.substr(9);
        if (util::crc32(body) != (uint32_t)strtoul(line.substr(0, 8).c_str(), nullptr, 16)) { bad_++; return; }
        auto p = util::split(body, '\t'); if (p.size() < 2 || p[0].empty()) { bad_++; return; }
        uint64_t id = strtoull(p[1].c_str(), nullptr, 10);
        if (p[0][0] == 'D') rows_.erase(id);
        else { Rec r; r.id = id; for (size_t i = 2; i < p.size(); i++) { auto e = p[i].find('='); if (e != std::string::npos) r.f[unesc(p[i].substr(0, e))] = unesc(p[i].substr(e + 1)); } rows_[id] = std::move(r); }
        next_ = std::max(next_, id + 1);
    }
    void sync() {
        std::error_code ec; auto sz = fs::file_size(path_, ec);
        if (ec) { if (!loaded_ || pos_) { rows_.clear(); next_ = 1; pos_ = 0; bad_ = 0; gen_.clear(); } loaded_ = true; lastSz_ = 0; return; }
        auto mt = fs::last_write_time(path_, ec);
        if (loaded_ && sz == lastSz_ && mt == lastMt_) return;
        std::ifstream in(path_, std::ios::binary); std::string hdr; std::getline(in, hdr);
        if (!loaded_ || hdr != gen_ || sz < pos_) { rows_.clear(); next_ = 1; bad_ = 0; gen_ = hdr; pos_ = hdr.empty() ? 0 : hdr.size() + 1; }
        in.clear(); in.seekg((std::streamoff)pos_); std::string line;
        while (std::getline(in, line)) { if (in.eof()) break; apply(line); pos_ = (size_t)in.tellg(); }
        loaded_ = true; lastSz_ = sz; lastMt_ = mt;
    }
    std::string name_, path_, gen_;
    std::map<uint64_t, Rec> rows_;
    uint64_t next_ = 1; size_t pos_ = 0, bad_ = 0; uintmax_t lastSz_ = 0; fs::file_time_type lastMt_{}; bool loaded_ = false;
};

class DB {
public:
    void open(const std::string& dir) { fs::create_directories(dir); dir_ = dir; DbLock::setPath((fs::path(dir) / ".lock").string()); }
    Table& operator[](const std::string& n) {
        auto it = t_.find(n); if (it == t_.end()) it = t_.emplace(n, std::make_unique<Table>(n, (fs::path(dir_) / (n + ".tbl")).string())).first; return *it->second;
    }
    const std::string& dir() const { return dir_; }
    std::string auditPath() const { return (fs::path(dir_) / "audit.log").string(); }
    std::string backup() {
        DbLock L; std::string dst = (fs::path(dir_) / "backups" / util::fmtStamp()).string(); fs::create_directories(dst);
        for (auto& e : fs::directory_iterator(dir_)) if (e.is_regular_file() && (e.path().extension() == ".tbl" || e.path().filename() == "audit.log")) fs::copy_file(e.path(), fs::path(dst) / e.path().filename(), fs::copy_options::overwrite_existing);
        return dst;
    }
    std::vector<std::string> backups() {
        std::vector<std::string> v; fs::path b = fs::path(dir_) / "backups"; if (!fs::exists(b)) return v;
        for (auto& e : fs::directory_iterator(b)) if (e.is_directory()) v.push_back(e.path().filename().string());
        std::sort(v.rbegin(), v.rend()); return v;
    }
    bool restore(const std::string& name) {
        DbLock L; fs::path src = fs::path(dir_) / "backups" / name; if (!fs::exists(src)) return false;
        for (auto& e : fs::directory_iterator(dir_)) if (e.is_regular_file() && (e.path().extension() == ".tbl" || e.path().filename() == "audit.log")) fs::remove(e.path());
        for (auto& e : fs::directory_iterator(src)) if (e.is_regular_file()) fs::copy_file(e.path(), fs::path(dir_) / e.path().filename(), fs::copy_options::overwrite_existing);
        return true;
    }
    std::vector<std::pair<std::string, uintmax_t>> files() {
        std::vector<std::pair<std::string, uintmax_t>> v;
        for (auto& e : fs::directory_iterator(dir_)) if (e.is_regular_file() && (e.path().extension() == ".tbl" || e.path().filename() == "audit.log")) v.push_back({e.path().filename().string(), e.file_size()});
        std::sort(v.begin(), v.end()); return v;
    }
private:
    std::string dir_; std::map<std::string, std::unique_ptr<Table>> t_;
};

// Tamper-evident audit trail: hash(prev | ts | actor | action | detail)
class Audit {
public:
    struct Entry { std::string ts, actor, action, detail, prev, hash; };
    void setPath(const std::string& p) { path_ = p; }
    void log(const std::string& actor, const std::string& action, const std::string& detail) {
        DbLock L; std::string prev = lastHash();
        std::string ts = std::to_string(util::now()), a = clean(actor), ac = clean(action), d = clean(detail);
        std::string h = util::sha256hex(prev + "|" + ts + "|" + a + "|" + ac + "|" + d);
        FILE* fp = fopen(path_.c_str(), "ab"); if (!fp) return;
        std::string ln = ts + "\t" + a + "\t" + ac + "\t" + d + "\t" + prev + "\t" + h + "\n";
        fwrite(ln.data(), 1, ln.size(), fp); syncFile(fp); fclose(fp);
    }
    std::vector<Entry> read() {
        std::vector<Entry> v; std::ifstream in(path_); std::string ln;
        while (std::getline(in, ln)) { auto p = util::split(ln, '\t'); if (p.size() == 6) v.push_back({p[0], p[1], p[2], p[3], p[4], p[5]}); else v.push_back({"0", "?", "MALFORMED", ln, "", ""}); }
        return v;
    }
    // returns {ok, entries, index of first bad entry (1-based, 0 if ok)}
    struct Result { bool ok; size_t total, badAt; std::string head; };
    Result verify() {
        auto v = read(); std::string prev = "GENESIS";
        for (size_t i = 0; i < v.size(); i++) {
            auto& e = v[i]; std::string h = util::sha256hex(e.prev + "|" + e.ts + "|" + e.actor + "|" + e.action + "|" + e.detail);
            if (e.prev != prev || h != e.hash) return {false, v.size(), i + 1, prev};
            prev = e.hash;
        }
        return {true, v.size(), 0, prev};
    }
private:
    static std::string clean(std::string s) { for (auto& c : s) if (c == '\t' || c == '\n' || c == '\r' || c == '|') c = ' '; return s; }
    std::string lastHash() {
        std::ifstream in(path_, std::ios::binary | std::ios::ate); if (!in) return "GENESIS";
        std::streamoff sz = in.tellg(); if (sz <= 0) return "GENESIS";
        std::streamoff from = std::max<std::streamoff>(0, sz - 4096); in.seekg(from); std::string buf((size_t)(sz - from), '\0'); in.read(&buf[0], (std::streamsize)buf.size());
        while (!buf.empty() && buf.back() == '\n') buf.pop_back();
        size_t nl = buf.rfind('\n'); std::string last = nl == std::string::npos ? buf : buf.substr(nl + 1);
        auto p = util::split(last, '\t'); return p.size() == 6 ? p[5] : "GENESIS";
    }
    std::string path_;
};
} // namespace store
