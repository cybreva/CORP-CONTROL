// core.hpp - application context: settings, RBAC, auth, notifications, shared helpers
#pragma once
#include "store.hpp"
#include "ui.hpp"

namespace app {
using store::Rec; using store::Fields; using store::Table;
using namespace ui;
using Row = std::vector<std::string>;

struct App { store::DB db; store::Audit audit; Rec user, emp; bool in = false; };
inline App& A() { static App a; return a; }
inline Table& T(const char* n) { return A().db[n]; }
inline std::string me() { return A().in ? A().user.s("username") : "system"; }
inline void audit(const std::string& action, const std::string& detail) { A().audit.log(me(), action, detail); }

// ---------- settings ----------
inline const std::map<std::string, std::string>& defaults() {
    static const std::map<std::string, std::string> d = {
        {"company", "My Company"}, {"currency", "₹"}, {"maintenance", "0"},
        {"allow_reimb", "1"}, {"sw_window", "1"}, {"auto_leave_days", "0"},
        {"exp_fin_threshold", "0"}, {"exp_window_days", "60"},
        {"idle_min", "15"}, {"max_attempts", "5"}, {"lock_min", "10"},
        {"work_week", "5"}, {"pf_pct", "0"}, {"tax_pct", "0"}};
    return d;
}
inline std::string S(const std::string& k) {
    auto r = T("settings").get(1); if (r && r->f.count(k)) return r->f.at(k);
    auto it = defaults().find(k); return it == defaults().end() ? "" : it->second;
}
inline long long SN(const std::string& k) { try { return std::stoll(S(k)); } catch (...) { return 0; } }
inline void setSetting(const std::string& k, const std::string& v) {
    auto& t = T("settings");
    if (!t.modify(1, [&](Rec& r) { r.set(k, v); return true; })) { Fields f; f[k] = v; t.insert(f); }
}
inline std::string cur() { return S("currency"); }
inline std::string M(long long paise) { return util::money(paise, cur()); }

// ---------- roles & permissions ----------
inline const std::vector<std::string>& roles() { static const std::vector<std::string> r = {"Employee", "Manager", "HR", "Finance", "Admin"}; return r; }
inline bool roleCan(const std::string& role, const std::string& perm) {
    static const std::map<std::string, std::set<std::string>> P = {
        {"Admin", {"*"}},
        {"HR", {"emp.manage", "leave.admin", "asset.manage", "sw.approve", "reports", "payroll.view", "att.view", "team.view"}},
        {"Finance", {"exp.finance", "exp.cats", "payroll.run", "payroll.view", "reports", "emp.view"}},
        {"Manager", {"team.view"}},
        {"Employee", {}}};
    auto it = P.find(role); if (it == P.end()) return false;
    return it->second.count("*") || it->second.count(perm);
}
inline bool can(const std::string& perm) { return A().in && roleCan(A().user.s("role"), perm); }
inline bool isAdmin() { return A().in && A().user.s("role") == "Admin"; }

// ---------- lookups ----------
inline std::optional<Rec> empById(uint64_t id) { return T("employees").get(id); }
inline std::string empName(uint64_t id) { auto e = empById(id); return e ? e->s("name") : "—"; }
inline std::string empCode(uint64_t id) { auto e = empById(id); return e ? e->s("code") : "—"; }
inline std::string empLabel(uint64_t id) { auto e = empById(id); return e ? e->s("name") + " (" + e->s("code") + ")" : "—"; }
inline std::string deptName(uint64_t id) { auto d = T("departments").get(id); return d ? d->s("name") : "—"; }
inline std::optional<Rec> userOfEmp(uint64_t empId) { return T("users").first([&](const Rec& r) { return (uint64_t)r.n("emp_id") == empId; }); }
inline uint64_t myEmp() { return A().emp.id; }
inline bool isMyReport(const Rec& employee) { return (uint64_t)employee.n("manager_id") == myEmp() && myEmp() != 0; }
inline std::string ymd(int64_t days) { return util::fmtDate(days); }
inline int64_t dayOf(const std::string& ymdStr) { int64_t d = 0; util::parseDate(ymdStr, d); return d; }
inline std::string niceYmd(const std::string& y) { int64_t d; return util::parseDate(y, d) ? util::niceDate(d) : y; }

// ---------- notifications ----------
inline void notifyUser(uint64_t userId, const std::string& text) { T("notifs").insert({{"user_id", std::to_string(userId)}, {"text", text}, {"ts", std::to_string(util::now())}, {"read", "0"}}); }
inline void notifyEmp(uint64_t empId, const std::string& text) { auto u = userOfEmp(empId); if (u) notifyUser(u->id, text); }
inline void notifyPerm(const std::string& perm, const std::string& text, uint64_t exceptUser = 0) {
    for (auto& u : T("users").where([&](const Rec& r) { return r.b("active") && roleCan(r.s("role"), perm) && r.id != exceptUser; })) notifyUser(u.id, text);
}
inline int unreadCount() { if (!A().in) return 0; uint64_t id = A().user.id; return (int)T("notifs").where([&](const Rec& r) { return (uint64_t)r.n("user_id") == id && !r.b("read"); }).size(); }

// ---------- form helpers ----------
inline bool askDate(Form& f, const std::string& label, std::string& out, const std::string& def = "today", bool optional = false) {
    FormOpt o; o.hint = "YYYY-MM-DD, DD-MM-YYYY or 'today'"; o.def = def; o.optional = optional;
    o.check = [](const std::string& s) { int64_t d; return util::parseDate(s, d) ? "" : "Not a valid date"; };
    std::string v; if (!f.ask(label, v, o)) return false;
    out = v.empty() ? "" : ymd(dayOf(v)); return true;
}
inline bool askMoney(Form& f, const std::string& label, long long& paise, bool optional = false, const std::string& def = "") {
    FormOpt o; o.hint = "Amount in " + cur() + " (e.g. 1500 or 1500.50)"; o.optional = optional; o.def = def;
    o.check = [](const std::string& s) { long long p; if (!util::parseMoney(s, p)) return "Enter a valid amount"; if (p <= 0) return "Amount must be greater than zero"; return ""; };
    std::string v; if (!f.ask(label, v, o)) return false;
    paise = 0; if (!v.empty()) util::parseMoney(v, paise); return true;
}
inline bool askInt(Form& f, const std::string& label, long long& out, long long lo, long long hi, const std::string& def = "", const std::string& hint = "") {
    FormOpt o; o.def = def; o.hint = hint.empty() ? "Whole number between " + std::to_string(lo) + " and " + std::to_string(hi) : hint;
    o.check = [=](const std::string& s) -> std::string { long long v; if (!util::parseInt(s, v)) return "Enter a whole number"; if (v < lo || v > hi) return "Must be between " + std::to_string(lo) + " and " + std::to_string(hi); return std::string(); };
    std::string v; if (!f.ask(label, v, o)) return false; util::parseInt(v, out); return true;
}
inline FormOpt reqText(const std::string& hint = "", size_t maxLen = 80) { FormOpt o; o.hint = hint; o.maxLen = maxLen; return o; }

// ---------- working-day maths ----------
inline int workDays(int64_t a, int64_t b) {
    int wk = (int)SN("work_week"), n = 0;
    for (int64_t d = a; d <= b; d++) { int w = util::weekday(d); bool off = (w == 0) || (wk <= 5 && w == 6); if (wk >= 7) off = false; if (!off) n++; }
    return n;
}

// ---------- employee picker ----------
inline uint64_t pickEmployee(const std::string& title, const std::function<bool(const Rec&)>& filter = nullptr, bool allowNone = false, bool* none = nullptr) {
    Crumb c(title); std::string q;
    while (true) {
        std::vector<Rec> list; std::vector<Row> rows;
        for (auto& e : T("employees").all()) {
            if (e.s("status") == "Exited" || (filter && !filter(e))) continue;
            if (!q.empty() && !util::icontains(e.s("name") + e.s("code") + e.s("designation"), q)) continue;
            list.push_back(e); rows.push_back({e.s("code"), e.s("name"), deptName((uint64_t)e.n("dept_id")), e.s("designation")});
        }
        PickOpt o; o.title = title; o.hot = {{'s', "Search"}}; if (allowNone) o.hot.push_back({'n', "None"}); o.empty = "No matching employees."; o.note = q.empty() ? "" : "Filter: \"" + q + "\"";
        auto p = pickTable({{"Code"}, {"Name"}, {"Department"}, {"Designation"}}, rows, o);
        if (p.key == 's') { Form f("Search employees"); std::string s; FormOpt fo; fo.optional = true; fo.hint = "Name, code or designation (blank clears)"; if (f.ask("Search", s, fo)) q = s; continue; }
        if (p.key == 'n' && allowNone) { if (none) *none = true; return 0; }
        if (p.idx < 0) return 0; return list[p.idx].id;
    }
}

// ---------- auth ----------
inline std::string passwordPolicy(const std::string& pw) {
    if (pw.size() < 8) return "Use at least 8 characters";
    bool l = false, d = false; for (char c : pw) { if (isalpha((unsigned char)c)) l = true; if (isdigit((unsigned char)c)) d = true; }
    return (l && d) ? "" : "Include both letters and digits";
}
inline void setPassword(uint64_t userId, const std::string& pw, bool mustChange) {
    std::string salt = util::randomHex(16), h = util::hashPassword(pw, salt);
    T("users").modify(userId, [&](Rec& r) { r.set("salt", salt).set("hash", h).set("must_change", mustChange ? 1 : 0).set("fails", 0).set("locked_until", 0); return true; });
}
inline bool askNewPassword(Form& f, std::string& out) {
    FormOpt a; a.mask = true; a.hint = "Min 8 characters, letters and digits"; a.check = passwordPolicy;
    std::string p1, p2; if (!f.ask("New password", p1, a)) return false;
    FormOpt b; b.mask = true; b.check = [&](const std::string& s) { return s == p1 ? "" : "Passwords do not match"; };
    if (!f.ask("Confirm password", p2, b)) return false; out = p1; return true;
}
inline void changeOwnPassword(bool forced) {
    Crumb c("Change password");
    Form f(forced ? "Set a new password to continue" : "Change password");
    std::string cur_, np;
    FormOpt o; o.mask = true;
    o.check = [&](const std::string& s) { return util::ctEq(util::hashPassword(s, A().user.s("salt")), A().user.s("hash")) ? "" : "Current password is incorrect"; };
    if (!f.ask("Current password", cur_, o)) return;
    if (!askNewPassword(f, np)) return;
    setPassword(A().user.id, np, false); A().user = *T("users").get(A().user.id);
    audit("PASSWORD_CHANGED", "self-service"); flash('o', "Password updated.");
}
inline void refreshCtx() {
    auto& x = ctx(); x.company = S("company"); x.maint = S("maintenance") == "1";
    x.unread = unreadCount();
    if (A().in) { x.user = A().emp.s("name", A().user.s("username")); x.role = A().user.s("role"); }
    term::idleLimit() = A().in ? (int)SN("idle_min") * 60 : 0;
}
inline std::vector<std::string> logo() {
    return {"╔═╗╔═╗╦═╗╔═╗  ╔═╗╔═╗╔╗╔╔╦╗╦═╗╔═╗╦    ", "║  ║ ║╠╦╝╠═╝  ║  ║ ║║║║ ║ ╠╦╝║ ║║  ++", "╚═╝╚═╝╩╚═╩    ╚═╝╚═╝╝╚╝ ╩ ╩╚═╚═╝╩═╝"};
}
inline std::string logoBlock(const std::string& tagline) {
    static const char* g[3] = {c::ACC, "\x1b[38;5;111m", c::ACC2}; std::string o = "\n"; auto l = logo();
    for (int i = 0; i < 3; i++) o += "    " + paint(g[i], std::string(c::BOLD) + l[i]) + "\n";
    return o + "    " + paint(c::MUTE, tagline) + "\n\n";
}
} // namespace app
