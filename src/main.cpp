// CORP-CONTROL++ v2 - terminal-native operations suite (HR · Finance · Assets · Approvals · Audit)
#include "admin.hpp"
#include <iostream>

using namespace app;
static const char* VERSION = "2.0.0";

// ---------------- first-run setup ----------------
static bool setupWizard() {
    Form f("Welcome — let's set up your company", logoBlock("First run detected. No data exists yet; you're creating the Administrator."));
    std::string company, curr, name, email, uname, pw; int tpl = 1;
    FormOpt cu = reqText("Symbol shown before amounts", 4); cu.def = "₹";
    FormOpt em = reqText("Administrator email", 80); em.check = [](const std::string& s) { return s.find('@') != std::string::npos && s.find('.', s.find('@')) != std::string::npos ? "" : "Enter a valid email"; };
    FormOpt un = reqText("3-30 chars: letters, digits, dot, dash, underscore", 30);
    un.check = [](const std::string& s) -> std::string { if (s.size() < 3) return "At least 3 characters"; for (char c : util::lower(s)) if (!(isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_')) return "Only letters, digits . - _ allowed"; return ""; };
    if (!f.ask("Company name", company, reqText("Legal or trading name", 60)) || !f.ask("Currency symbol", curr, cu) || !f.ask("Your full name", name, reqText("Administrator's name", 60)) || !f.ask("Your email", email, em) || !f.ask("Username", uname, un) || !askNewPassword(f, pw)) return false;
    if (!f.choose("Leave types", {"Start empty — I'll configure them", "Add common set: Casual 12 · Sick 10 · Earned 15 · Unpaid"}, tpl, 0)) return false;
    Fields st = {{"company", company}, {"currency", curr}}; T("settings").insert(st);
    uint64_t eid = hr::createEmployee({{"name", name}, {"email", email}, {"designation", "System Administrator"}, {"join_date", ymd(util::todayDays())}, {"status", "Active"}, {"dept_id", "0"}, {"manager_id", "0"}, {"salary", "0"}});
    std::string salt = util::randomHex(16);
    T("users").insertUnique({{"username", util::lower(uname)}, {"role", "Admin"}, {"emp_id", std::to_string(eid)}, {"active", "1"}, {"fails", "0"}, {"locked_until", "0"}, {"must_change", "0"}, {"salt", salt}, {"hash", util::hashPassword(pw, salt)}, {"created", std::to_string(util::now())}}, {"username"});
    if (tpl == 1) { struct LT { const char* n; int d, p; } l[] = {{"Casual Leave", 12, 1}, {"Sick Leave", 10, 1}, {"Earned Leave", 15, 1}, {"Unpaid Leave", 0, 0}}; for (auto& x : l) T("leave_types").insert({{"name", x.n}, {"days", std::to_string(x.d)}, {"paid", std::to_string(x.p)}}); }
    A().audit.log(util::lower(uname), "SETUP_COMPLETED", "company=" + company); flash('o', "Setup complete. Sign in with your new Administrator account.\n\nTip: a daily backup (Administration › Backup) is the best insurance for production data.", "You're all set"); return true;
}

// ---------------- login ----------------
static void logout() { if (A().in) audit("LOGOUT", ""); A().in = false; A().user = Rec(); A().emp = Rec(); refreshCtx(); }
static bool login() {
    Form f("Sign in", logoBlock("Secure sign-in")); std::string u, p; FormOpt po; po.mask = true;
    if (!f.ask("Username", u, reqText("", 30)) || !f.ask("Password", p, po)) return false;
    u = util::lower(util::trim(u)); auto rec = T("users").first([&](const Rec& r) { return r.s("username") == u; });
    if (!rec) { util::hashPassword(p, "decoy"); audit("LOGIN_FAILED", "unknown user '" + u + "'"); flash('e', "Invalid username or password."); return false; }
    long long now = util::now();
    if (rec->n("locked_until") > now) { audit("LOGIN_BLOCKED", u + " account locked"); flash('e', "This account is temporarily locked after repeated failures. Try again in " + std::to_string((rec->n("locked_until") - now + 59) / 60) + " minute(s), or ask an Administrator to unlock it.", "Account locked"); return false; }
    if (!util::ctEq(util::hashPassword(p, rec->s("salt")), rec->s("hash"))) {
        long long fails = rec->n("fails") + 1; bool lock = fails >= SN("max_attempts");
        T("users").modify(rec->id, [&](Rec& r) { r.set("fails", lock ? 0 : fails); if (lock) r.set("locked_until", now + SN("lock_min") * 60); return true; });
        audit(lock ? "ACCOUNT_LOCKED" : "LOGIN_FAILED", u + " attempt " + std::to_string(fails));
        flash('e', lock ? "Too many failed attempts. The account is locked for " + std::to_string(SN("lock_min")) + " minute(s)." : "Invalid username or password. (" + std::to_string(SN("max_attempts") - fails) + " attempt(s) left)"); return false;
    }
    if (!rec->b("active")) { audit("LOGIN_BLOCKED", u + " disabled"); flash('e', "This account is disabled. Please contact your administrator.", "Account disabled"); return false; }
    if (S("maintenance") == "1" && rec->s("role") != "Admin") { audit("LOGIN_BLOCKED", u + " maintenance mode"); flash('w', "The system is under maintenance. Please try again later.", "Service unavailable"); return false; }
    T("users").modify(rec->id, [&](Rec& r) { r.set("fails", 0).set("locked_until", 0).set("last_login", now); return true; });
    A().user = *T("users").get(rec->id); auto e = empById((uint64_t)A().user.n("emp_id")); A().emp = e ? *e : Rec(); if (!e) A().emp.set("name", u);
    A().in = true; audit("LOGIN", "ok"); refreshCtx();
    while (A().user.b("must_change")) { hr::changeOwnPassword(true); A().user = *T("users").get(A().user.id); if (A().user.b("must_change") && !confirm("You must set a new password to continue. Try again? (No = sign out)", true)) { logout(); return false; } }
    return true;
}

// ---------------- dashboard ----------------
static std::string greeting() { int h = util::localtm(util::now()).tm_hour; return h < 12 ? "Good morning" : h < 17 ? "Good afternoon" : "Good evening"; }
static std::string dashboard(size_t approvals) {
    std::string first = A().emp.s("name"); first = first.substr(0, first.find(' '));
    std::string o = "  " + paint(c::BOLD, greeting() + ", " + first + ".") + paint(c::MUTE, "  " + util::niceDate(util::todayDays())) + "\n\n";
    std::vector<Tile> t; bool appr = can("leave.admin") || can("exp.finance") || can("sw.approve") || can("team.view") || isAdmin();
    size_t mine = 0; for (auto& l : T("leaves").where([&](const Rec& r) { return (uint64_t)r.n("emp_id") == myEmp() && r.s("status") == "Pending"; })) { (void)l; mine++; }
    mine += T("expenses").where([&](const Rec& r) { return (uint64_t)r.n("emp_id") == myEmp() && r.s("status").rfind("Pending", 0) == 0; }).size() + T("sw_requests").where([&](const Rec& r) { return (uint64_t)r.n("emp_id") == myEmp() && r.s("status") == "Pending"; }).size();
    t.push_back(appr ? Tile{"awaiting your approval", std::to_string(approvals), approvals ? c::WARN : c::OK} : Tile{"my open requests", std::to_string(mine), mine ? c::WARN : c::OK});
    t.push_back({"unread notifications", std::to_string(ctx().unread), ctx().unread ? c::WARN : c::MUTE});
    if (can("emp.manage") || isAdmin()) {
        size_t act = T("employees").where([](const Rec& e) { return e.s("status") != "Exited"; }).size(), away = 0; int64_t d = util::todayDays();
        for (auto& e : T("employees").all()) if (e.s("status") != "Exited" && hr::onLeave(e.id, d)) away++;
        t.push_back({"active employees", std::to_string(act), c::ACC}); t.push_back({"on leave today", std::to_string(away), c::ACC2});
    } else if (can("exp.finance")) {
        long long due = 0; size_t n = 0; for (auto& e : T("expenses").where([](const Rec& r) { return r.s("status") == "Approved"; })) { due += e.n("amount"); n++; }
        t.push_back({"payouts due (" + std::to_string(n) + " claims)", M(due), n ? c::WARN : c::OK});
        t.push_back({"claims in review", std::to_string(T("expenses").where([](const Rec& r) { return r.s("status").rfind("Pending", 0) == 0; }).size()), c::ACC});
    } else {
        long long left = 0; int y = hr::thisYear(); for (auto& lt : T("leave_types").where([](const Rec& r) { return r.b("paid"); })) left += hr::available(myEmp(), lt.id, y);
        auto att = hr::attendanceOn(myEmp(), ymd(util::todayDays()));
        t.push_back({"paid leave days left", std::to_string(left), c::ACC}); t.push_back({"today", !att ? "Not in" : att->n("out") ? "Done" : "Working", !att ? c::MUTE : c::OK});
    }
    o += tiles(t);
    if (can("emp.manage") || isAdmin()) { // onboarding checklist for fresh installs
        bool d = T("departments").count(), e = T("employees").count() > 1, l = T("leave_types").count(), x = T("exp_cats").count();
        if (!(d && e && l && x)) { auto mk = [](bool ok, const std::string& s) { return (ok ? paint(c::OK, "✔ ") : paint(c::WARN, "○ ")) + (ok ? paint(c::MUTE, s) : s); }; o += "\n" + box({mk(d, "Create departments        People › Departments"), mk(l, "Define leave types         People › Leave types"), mk(x, "Add expense categories      Finance › Categories"), mk(e, "Add your employees         People › Employees")}, std::min(W(), 70), "Getting started", c::ACC2); }
    }
    return o;
}

// ---------------- menus ----------------
static void workspace() {
    Crumb c("My workspace"); int last = 0;
    while (true) {
        int s = menu("My workspace", {{"Leave", "apply, track, balances"}, {"Attendance", "check in / out, history"}, {"Expense claims", "file & track reimbursements"}, {"Software requests", "ask for licenses & tools"}, {"My assets", "devices assigned to me"}, {"My payslips", "salary history"}}, "", last);
        if (s < 0) return; last = s;
        if (s == 0) hr::myLeaves(); else if (s == 1) hr::attendanceSelf(); else if (s == 2) fin::myExpenses(); else if (s == 3) ops::mySoftware(); else if (s == 4) ops::myAssets(); else fin::payslips(false);
    }
}
static void people() {
    Crumb c("People"); int last = 0;
    while (true) {
        int s = menu("People", {{"Employees", "directory, logins, offboarding", can("emp.manage") || can("emp.view")}, {"Departments", "", can("emp.manage")}, {"Leave types", "policies & entitlements", can("leave.admin")}, {"Adjust leave balance", "", can("leave.admin")}, {"All leave records", "", can("leave.admin")}, {"Attendance register", "daily roll-call", can("att.view")}}, "", last);
        if (s < 0) return; last = s;
        if (s == 0) hr::employees(); else if (s == 1) hr::departments(); else if (s == 2) hr::leaveTypes(); else if (s == 3) hr::adjustBalance(); else if (s == 4) hr::allLeaves(); else hr::attendanceRegister();
    }
}
static void financeMenu() {
    Crumb c("Finance"); int last = 0;
    while (true) {
        int s = menu("Finance", {{"Expense claims", "review, approve, pay out", can("exp.finance")}, {"Expense categories", "limits per claim", can("exp.cats")}, {"Run payroll", "generate monthly payslips", can("payroll.run")}, {"Payslips", "all employees", can("payroll.view")}, {"Expense analytics", "by status, category, department"}}, "", last);
        if (s < 0) return; last = s;
        if (s == 0) fin::allExpenses(); else if (s == 1) fin::categories(); else if (s == 2) fin::runPayroll(); else if (s == 3) fin::payslips(true); else fin::analytics();
    }
}
static void home() {
    int last = 0;
    while (true) {
        refreshCtx();
        auto u = T("users").get(A().user.id);
        if (!u || !u->b("active")) { logout(); flash('w', "Your account has been disabled.", "Signed out"); return; }
        if (S("maintenance") == "1" && !isAdmin()) { logout(); flash('w', "The system was placed in maintenance mode by an administrator. You have been signed out.", "Maintenance"); return; }
        A().user = *u; size_t ap = ops::buildApprovals().size(); bool appr = can("leave.admin") || can("exp.finance") || can("sw.approve") || can("team.view") || isAdmin();
        std::vector<MenuItem> it; std::vector<std::function<void()>> act;
        auto add = [&](const std::string& l, const std::string& h, bool en, std::function<void()> f, const std::string& b = "") { it.push_back({l, h, en, b}); act.push_back(f); };
        if (appr) add("Approvals", "leave · expenses · payouts · software", true, ops::approvals, ap ? paint(c::WARN, "● " + std::to_string(ap) + " waiting") : "");
        add("My workspace", "leave, attendance, expenses, payslips", true, workspace);
        if (can("team.view") && !can("att.view")) add("My team", "today's attendance", true, hr::attendanceRegister);
        if (can("emp.manage") || can("emp.view") || can("leave.admin") || can("att.view")) add("People", "employees, departments, leave admin", true, people);
        if (can("exp.finance") || can("payroll.view") || can("exp.cats")) add("Finance", "claims, payroll, analytics", true, financeMenu);
        if (can("asset.manage")) add("Assets", "register, assign, return", true, ops::assets);
        if (can("reports")) add("Reports", "analytics & CSV export", true, ops::reports);
        add("Inbox", "notifications", true, ops::inbox, ctx().unread ? paint(c::WARN, "● " + std::to_string(ctx().unread) + " new") : "");
        add("My profile", "details & password", true, ops::profile);
        if (isAdmin()) add("Administration", "policies, users, audit, backup", true, adm::panel);
        add("Sign out", "", true, [] {});
        int s = menu("", it, dashboard(ap), last);
        if (s < 0) { if (confirm("Sign out of CORP-CONTROL++?", true, c::ACC)) { logout(); return; } continue; }
        if (it[s].label == "Sign out") { logout(); return; }
        last = s; act[s]();
    }
}

// ---------------- entry ----------------
static int resetPasswordCli(const std::string& dir, const std::string& user) {
    A().db.open(dir); A().audit.setPath(A().db.auditPath());
    auto u = T("users").first([&](const Rec& r) { return r.s("username") == util::lower(user); });
    if (!u) { fprintf(stderr, "No such user: %s\n", user.c_str()); return 1; }
    std::string tp = util::tempPassword(); setPassword(u->id, tp, true); T("users").modify(u->id, [](Rec& r) { r.set("active", 1); return true; });
    A().audit.log("cli", "PASSWORD_RESET_CLI", util::lower(user)); printf("Temporary password for '%s': %s\n(change required at next sign-in)\n", user.c_str(), tp.c_str()); return 0;
}
int main(int argc, char** argv) {
    std::string dir = getenv("CORPCONTROL_DATA") ? getenv("CORPCONTROL_DATA") : "corpdata";
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) dir = argv[++i];
        else if (a == "--version") { printf("CORP-CONTROL++ %s\n", VERSION); return 0; }
        else if (a == "--reset-password" && i + 1 < argc) return resetPasswordCli(dir, argv[++i]);
        else { printf("CORP-CONTROL++ %s\n\nUsage: corpcontrol [--data DIR] [--reset-password USER] [--version]\n\n  --data DIR            data directory (default ./corpdata, or $CORPCONTROL_DATA)\n  --reset-password USER emergency recovery: issue a temporary password (needs file access)\n", VERSION); return a == "--help" ? 0 : 1; }
    }
    try { A().db.open(dir); A().audit.setPath(A().db.auditPath()); } catch (std::exception& e) { fprintf(stderr, "Cannot open data directory '%s': %s\n", dir.c_str(), e.what()); return 1; }
    term::init();
    try {
        while (true) {
            try {
                if (T("users").count() == 0) { refreshCtx(); if (!setupWizard()) { if (confirm("Exit setup? Nothing has been saved.", true)) break; continue; } }
                refreshCtx();
                int s = menu("", {{"Sign in", "with your company account"}, {"About", "version & data location"}, {"Exit", ""}}, logoBlock("Policy-driven operations platform  ·  v" + std::string(VERSION)));
                if (s == 0) { if (login()) home(); }
                else if (s == 1) card("About CORP-CONTROL++", {{"Version", VERSION}, {"Data directory", std::filesystem::absolute(A().db.dir()).string()}, {"Users", std::to_string(T("users").count())}, {"Employees", std::to_string(T("employees").count())}, {"Audit entries", std::to_string(A().audit.read().size())}});
                else if (s == 2 || s < 0) { if (confirm("Exit CORP-CONTROL++?", true, c::ACC)) break; }
            } catch (SessionTimeout&) { logout(); ctx().crumbs.clear(); term::idleLimit() = 0; flash('w', "You were signed out after a period of inactivity.", "Session expired"); }
        }
    } catch (std::exception& e) { term::shutdown(); fprintf(stderr, "Fatal error: %s\n", e.what()); return 1; }
    term::shutdown(); printf("Goodbye.\n"); return 0;
}
