// admin.hpp - policies, kill-switch, user management, audit trail, backup & storage
#pragma once
#include "ops.hpp"

namespace adm {
using namespace app;

// ================= policies =================
struct Pol { std::string key, label, type, desc; long long lo, hi; };
inline const std::vector<Pol>& policies() {
    static const std::vector<Pol> p = {
        {"company", "Company name", "text", "Shown in the header and reports", 0, 0},
        {"currency", "Currency symbol", "text", "Prefix for all amounts (e.g. ₹, $, Rs.)", 0, 0},
        {"allow_reimb", "Reimbursement claims", "bool", "Employees may file expense claims", 0, 0},
        {"sw_window", "Software request window", "bool", "Employees may request software / licenses", 0, 0},
        {"auto_leave_days", "Auto-approve leave ≤ N days", "int", "Paid leave up to N working days is approved instantly (0 = off)", 0, 30},
        {"exp_fin_threshold", "Finance review above", "money", "Claims above this need Finance review after manager (0 = all claims)", 0, 0},
        {"exp_window_days", "Claim window (days)", "int", "Max age of an expense when it is filed (0 = unlimited)", 0, 365},
        {"work_week", "Working days / week", "int", "5 = Mon-Fri, 6 = Mon-Sat, 7 = every day", 5, 7},
        {"pf_pct", "Provident fund %", "int", "Deducted from payable salary in payroll", 0, 50},
        {"tax_pct", "Income tax %", "int", "Flat TDS-style deduction in payroll", 0, 60},
        {"idle_min", "Auto sign-out (minutes)", "int", "Idle time before the session closes", 1, 240},
        {"max_attempts", "Failed logins before lock", "int", "Wrong passwords allowed before the account locks", 3, 20},
        {"lock_min", "Lockout duration (min)", "int", "How long a locked account stays locked", 1, 1440}};
    return p;
}
inline std::string polValue(const Pol& p) {
    std::string v = S(p.key);
    if (p.type == "bool") return v == "1" ? paint(c::OK, "● ON") : paint(c::BAD, "○ OFF");
    if (p.type == "money") return M(SN(p.key));
    return v;
}
inline void policyPanel() {
    Crumb c("Policies"); int start = 0;
    while (true) {
        std::vector<Row> rows; for (auto& p : policies()) rows.push_back({p.label, polValue(p), p.desc});
        PickOpt o; o.title = "Company policies — applied instantly, system-wide"; o.start = start; o.note = "Enter on a switch toggles it; Enter on a value edits it. Every change is written to the audit trail.";
        auto pk = pickTable({{"Policy", 30}, {"Value", 16}, {"What it does", 70}}, rows, o); if (pk.idx < 0) return; start = pk.idx; const Pol& p = policies()[pk.idx];
        std::string before = S(p.key), after;
        if (p.type == "bool") after = before == "1" ? "0" : "1";
        else {
            Form f("Edit: " + p.label); std::string v;
            if (p.type == "int") { long long n; if (!askInt(f, p.label, n, p.lo, p.hi, before)) continue; after = std::to_string(n); }
            else if (p.type == "money") { long long n; if (!askMoney(f, p.label, n, true, before == "0" ? "" : std::to_string(SN(p.key) / 100))) continue; after = std::to_string(n); }
            else { FormOpt o2 = reqText(p.desc, 40); o2.def = before; if (!f.ask(p.label, v, o2)) continue; after = v; }
        }
        if (before == after) continue; setSetting(p.key, after); audit("POLICY_CHANGED", p.key + ": " + before + " → " + after); refreshCtx();
    }
}
inline void killSwitch() {
    Crumb c("Kill-switch"); bool on = S("maintenance") == "1";
    std::string pre = "\n" + box({"", on ? "  " + paint(c::WARN, "● MAINTENANCE MODE IS ACTIVE") + " — only Admins can sign in." : "  " + paint(c::OK, "● System is online") + " — all roles can sign in.", ""}, std::min(W(), 84), "Service status", on ? c::WARN : c::OK);
    int s = menu("", {{on ? "Bring system back ONLINE" : "Activate MAINTENANCE mode", on ? "re-open for all users" : "locks out every non-admin user"}}, pre); if (s != 0) return;
    if (!on && !confirm("Activate maintenance mode? All non-admin users will be signed out at their next action and cannot sign in until you re-open the system.", false, c::WARN)) return;
    setSetting("maintenance", on ? "0" : "1"); audit(on ? "SERVICE_ONLINE" : "SERVICE_KILL_SWITCH", on ? "system brought online" : "maintenance mode activated"); refreshCtx(); flash('o', on ? "System is back online." : "Maintenance mode activated.");
}

// ================= users =================
inline int activeAdmins() { return (int)T("users").where([](const Rec& u) { return u.s("role") == "Admin" && u.b("active"); }).size(); }
inline void userCard(uint64_t id) {
    while (true) {
        auto uo = T("users").get(id); if (!uo) return; Rec u = *uo; bool locked = u.n("locked_until") > util::now(); bool self = id == A().user.id;
        std::string st = !u.b("active") ? "Disabled" : locked ? "Locked" : "Active";
        int k = card(u.s("username"), {{"Username", u.s("username")}, {"Employee", empLabel((uint64_t)u.n("emp_id"))}, {"Role", u.s("role")}, {"Status", strip(badge(st))}, {"Failed attempts", std::to_string(u.n("fails"))}, {"Last sign-in", util::fmtTs(u.n("last_login"))}, {"Password change", u.b("must_change") ? "required at next login" : "—"}}, {{'r', "Reset password"}, {'c', "Change role"}, {'t', u.b("active") ? "Disable" : "Enable"}, {'u', "Unlock"}});
        if (!k) return;
        if (k == 'r') { if (confirm("Reset password for " + u.s("username") + "?")) { std::string tp = util::tempPassword(); setPassword(id, tp, true); audit("PASSWORD_RESET", u.s("username")); flash('o', "Temporary password:   " + tp + "\n\nShown once. The user must change it at next sign-in.", "Password reset"); } }
        else if (k == 'c') {
            if (self) { flash('e', "You cannot change your own role."); continue; }
            Form f("Change role — " + u.s("username")); int ri = 0; int def = 0; for (size_t i = 0; i < roles().size(); i++) if (roles()[i] == u.s("role")) def = (int)i;
            if (!f.choose("New role", roles(), ri, def)) continue; if (roles()[ri] == u.s("role")) continue;
            if (u.s("role") == "Admin" && activeAdmins() <= 1) { flash('e', "At least one active Admin must remain."); continue; }
            T("users").modify(id, [&](Rec& r) { r.set("role", roles()[ri]); return true; }); audit("ROLE_CHANGED", u.s("username") + ": " + u.s("role") + " → " + roles()[ri]); flash('o', "Role updated.");
        } else if (k == 't') {
            if (self) { flash('e', "You cannot disable your own account."); continue; }
            if (u.b("active") && u.s("role") == "Admin" && activeAdmins() <= 1) { flash('e', "At least one active Admin must remain."); continue; }
            T("users").modify(id, [&](Rec& r) { r.set("active", u.b("active") ? 0 : 1); return true; }); audit(u.b("active") ? "USER_DISABLED" : "USER_ENABLED", u.s("username")); flash('o', u.b("active") ? "User disabled." : "User enabled.");
        } else if (k == 'u') { T("users").modify(id, [&](Rec& r) { r.set("fails", 0).set("locked_until", 0); return true; }); audit("USER_UNLOCKED", u.s("username")); flash('o', "Account unlocked."); }
    }
}
inline void users() {
    Crumb c("Users & roles");
    while (true) {
        auto us = T("users").all(); std::vector<Row> rows;
        for (auto& u : us) { bool locked = u.n("locked_until") > util::now(); rows.push_back({u.s("username"), empName((uint64_t)u.n("emp_id")), u.s("role"), badge(!u.b("active") ? "Disabled" : locked ? "Locked" : "Active"), util::fmtTs(u.n("last_login"))}); }
        PickOpt o; o.title = "User accounts"; o.note = "New logins are created from the employee's page (People › Employees).";
        auto p = pickTable({{"Username"}, {"Employee"}, {"Role"}, {"Status"}, {"Last sign-in"}}, rows, o); if (p.idx < 0) return; userCard(us[p.idx].id);
    }
}

// ================= audit trail =================
inline void auditTrail() {
    Crumb c("Audit trail"); std::string q;
    while (true) {
        auto es = A().audit.read(); std::vector<Row> rows;
        for (auto it = es.rbegin(); it != es.rend() && rows.size() < 1000; ++it) { std::string t = it->actor + it->action + it->detail; if (!q.empty() && !util::icontains(t, q)) continue; int64_t ts = 0; try { ts = std::stoll(it->ts); } catch (...) {} rows.push_back({util::fmtTs(ts), it->actor, it->action, it->detail}); }
        PickOpt o; o.title = "Audit trail — newest first (" + std::to_string(es.size()) + " entries)"; o.selectable = false; o.hot = {{'v', "Verify integrity"}, {'f', "Filter"}}; o.empty = "No entries match."; o.note = q.empty() ? "Entries are hash-chained: editing or deleting any past entry is detectable." : "Filter: \"" + q + "\"";
        auto p = pickTable({{"Time", 16}, {"User", 16}, {"Action", 26}, {"Detail", 70}}, rows, o); if (p.key == 0) return;
        if (p.key == 'f') { Form f("Filter audit trail"); std::string s; FormOpt fo; fo.optional = true; fo.hint = "User, action or text (blank clears)"; if (f.ask("Filter", s, fo)) q = s; }
        else if (p.key == 'v') { auto r = A().audit.verify(); if (r.ok) flash('o', "All " + std::to_string(r.total) + " entries verified. Chain is intact.\n\nHead hash: " + r.head.substr(0, 32) + "…", "Integrity verified"); else flash('e', "TAMPERING DETECTED at entry #" + std::to_string(r.badAt) + " of " + std::to_string(r.total) + ". The log was edited, reordered or truncated after that point.", "Integrity check FAILED"); audit("AUDIT_VERIFIED", r.ok ? "ok" : "FAILED at #" + std::to_string(r.badAt)); }
    }
}

// ================= backup & storage =================
inline void backupPanel() {
    Crumb c("Backup & storage");
    while (true) {
        int s = menu("Backup & storage", {{"Create backup now", "snapshot of all data + audit log"}, {"Restore from backup", "replaces ALL current data"}, {"Storage health", "tables, sizes, integrity"}, {"Compact storage", "reclaim space (safe)"}}); if (s < 0) return;
        if (s == 0) { std::string p = A().db.backup(); audit("BACKUP_CREATED", p); flash('o', "Backup saved to:\n" + std::filesystem::absolute(p).string(), "Backup complete"); }
        else if (s == 1) {
            auto bs = A().db.backups(); if (bs.empty()) { flash('i', "No backups found. Create one first."); continue; }
            Form f("Restore backup"); int i = 0; if (!f.choose("Backup", bs, i)) continue;
            if (!confirm("Restore \"" + bs[i] + "\"? ALL current data will be replaced by that snapshot and the app will close. A safety backup of the current state is taken first.", false, c::BAD)) continue;
            std::string safety = A().db.backup(); if (!A().db.restore(bs[i])) { flash('e', "Restore failed."); continue; }
            A().audit.log(me(), "BACKUP_RESTORED", bs[i] + " (safety copy: " + safety + ")"); term::shutdown(); printf("Restored backup %s. Please start CORP-CONTROL++ again.\n", bs[i].c_str()); std::exit(0);
        } else if (s == 2) {
            std::vector<Row> rows; size_t bad = 0; for (auto& f : A().db.files()) { std::string n = f.first; if (n == "audit.log") { auto v = A().audit.verify(); rows.push_back({"audit.log", std::to_string(v.total), std::to_string(f.second / 1024) + " KB", v.ok ? paint(c::OK, "chain OK") : paint(c::BAD, "TAMPERED")}); continue; } n = n.substr(0, n.size() - 4); auto& t = T(n.c_str()); bad += t.badLines(); rows.push_back({n, std::to_string(t.count()), std::to_string(t.fileSize() / 1024) + " KB", t.badLines() ? paint(c::WARN, std::to_string(t.badLines()) + " corrupt line(s) skipped") : paint(c::OK, "healthy")}); }
            PickOpt o; o.title = "Storage health"; o.selectable = false; o.note = "Data directory: " + std::filesystem::absolute(A().db.dir()).string(); pickTable({{"Table"}, {"Records", 10, true}, {"Size", 10, true}, {"Integrity"}}, rows, o);
        } else if (s == 3) {
            if (!confirm("Compact all tables? Other terminals using this system should be restarted afterwards.", true, c::ACC)) continue;
            A().db.backup(); for (auto& f : A().db.files()) if (f.first != "audit.log") T(f.first.substr(0, f.first.size() - 4).c_str()).compact(); audit("STORAGE_COMPACTED", "all tables"); flash('o', "Storage compacted. A backup was taken first.");
        }
    }
}

inline void panel() {
    Crumb c("Administration");
    while (true) {
        bool on = S("maintenance") == "1";
        int s = menu("Administration", {{"Company policies", "switches & limits, applied instantly"}, {"Kill-switch", on ? "maintenance mode is ON" : "take the system offline", true, on ? paint(c::WARN, "● MAINTENANCE") : ""}, {"Users & roles", "reset passwords, roles, unlock"}, {"Audit trail", "tamper-evident activity log"}, {"Backup & storage", "backup, restore, health"}});
        if (s < 0) return; if (s == 0) policyPanel(); else if (s == 1) killSwitch(); else if (s == 2) users(); else if (s == 3) auditTrail(); else backupPanel();
    }
}
} // namespace adm
