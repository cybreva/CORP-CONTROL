// hr.hpp - departments, employees, logins, leave management, attendance
#pragma once
#include "core.hpp"

namespace hr {
using namespace app;

inline uint64_t createEmployee(Fields f) {
    auto& t = T("employees"); uint64_t id = t.insert(f); char b[32]; snprintf(b, sizeof b, "EMP-%04llu", (unsigned long long)id);
    t.modify(id, [&](Rec& r) { r.set("code", b); return true; }); return id;
}

// ================= departments =================
inline void departments() {
    Crumb c("Departments");
    while (true) {
        auto ds = T("departments").all(); std::vector<Row> rows;
        for (auto& d : ds) { size_t hc = T("employees").where([&](const Rec& e) { return (uint64_t)e.n("dept_id") == d.id && e.s("status") != "Exited"; }).size(); rows.push_back({d.s("name"), std::to_string(hc), d.n("budget") ? M(d.n("budget")) : "—"}); }
        PickOpt o; o.title = "Departments"; o.hot = {{'a', "Add"}}; o.empty = "No departments yet. Press A to create the first one.";
        auto p = pickTable({{"Department"}, {"Active staff", 12, true}, {"Monthly budget", 18, true}}, rows, o);
        if (p.idx < 0 && p.key == 0) return;
        if (p.key == 'a') {
            Form f("New department"); std::string name; long long budget = 0;
            FormOpt n = reqText("e.g. Engineering, Sales, Operations", 40);
            n.check = [](const std::string& s) { return T("departments").first([&](const Rec& r) { return util::lower(r.s("name")) == util::lower(s); }) ? "A department with this name exists" : ""; };
            if (!f.ask("Name", name, n) || !askMoney(f, "Monthly budget", budget, true)) continue;
            T("departments").insert({{"name", name}, {"budget", std::to_string(budget)}}); audit("DEPT_CREATED", name); flash('o', "Department \"" + name + "\" created.");
        } else if (p.idx >= 0) {
            Rec d = ds[p.idx];
            int k = card(d.s("name"), {{"Monthly budget", d.n("budget") ? M(d.n("budget")) : "not set"}}, {{'e', "Edit"}, {'d', "Delete"}});
            if (k == 'e') {
                Form f("Edit department"); std::string name; long long budget = 0; FormOpt n = reqText("", 40); n.def = d.s("name");
                if (!f.ask("Name", name, n) || !askMoney(f, "Monthly budget", budget, true, d.n("budget") ? std::to_string(d.n("budget") / 100) : "")) continue;
                T("departments").modify(d.id, [&](Rec& r) { r.set("name", name).set("budget", budget); return true; }); audit("DEPT_UPDATED", name); flash('o', "Department updated.");
            } else if (k == 'd') {
                if (!T("employees").where([&](const Rec& e) { return (uint64_t)e.n("dept_id") == d.id; }).empty()) { flash('e', "This department still has employees assigned. Move them first."); continue; }
                if (confirm("Delete department \"" + d.s("name") + "\"?")) { T("departments").remove(d.id); audit("DEPT_DELETED", d.s("name")); flash('o', "Department deleted."); }
            }
        }
    }
}

// ================= employees =================
inline std::string suggestUsername(const std::string& name) {
    std::string u; for (char c : util::lower(name)) { if (isalnum((unsigned char)c)) u += c; else if (c == ' ' && !u.empty() && u.back() != '.') u += '.'; }
    while (!u.empty() && u.back() == '.') u.pop_back(); return u;
}
inline void createLogin(const Rec& e) {
    Crumb c("Login");
    auto ex = userOfEmp(e.id);
    if (ex) {
        if (!confirm("\"" + e.s("name") + "\" already has the login '" + ex->s("username") + "'. Reset the password to a new temporary one?")) return;
        std::string tp = util::tempPassword(); setPassword(ex->id, tp, true); audit("PASSWORD_RESET", ex->s("username"));
        flash('o', "Temporary password for " + ex->s("username") + ":   " + tp + "\n\nThey must change it at next sign-in. It will not be shown again.", "Password reset"); return;
    }
    Form f("Create login for " + e.s("name")); std::string uname; int ri = 0;
    FormOpt u = reqText("3-30 chars: letters, digits, dot, dash, underscore", 30); u.def = suggestUsername(e.s("name"));
    u.check = [](const std::string& s) { std::string l = util::lower(s); if (l.size() < 3) return std::string("At least 3 characters"); for (char ch : l) if (!(isalnum((unsigned char)ch) || ch == '.' || ch == '-' || ch == '_')) return std::string("Only letters, digits . - _ allowed"); if (T("users").first([&](const Rec& r) { return r.s("username") == l; })) return std::string("Username already taken"); return std::string(); };
    if (!f.ask("Username", uname, u)) return;
    std::vector<std::string> rs; for (auto& r : roles()) if (isAdmin() || r == "Employee" || r == "Manager") rs.push_back(r);
    if (!f.choose("Role", rs, ri)) return;
    uname = util::lower(uname); std::string tp = util::tempPassword(), salt = util::randomHex(16);
    uint64_t id = T("users").insertUnique({{"username", uname}, {"role", rs[ri]}, {"emp_id", std::to_string(e.id)}, {"active", "1"}, {"fails", "0"}, {"locked_until", "0"}, {"must_change", "1"}, {"salt", salt}, {"hash", util::hashPassword(tp, salt)}, {"created", std::to_string(util::now())}}, {"username"});
    if (!id) { flash('e', "That username was just taken. Try again."); return; }
    audit("LOGIN_CREATED", uname + " role=" + rs[ri] + " for " + e.s("code"));
    flash('o', "Login created.\n\nUsername:            " + uname + "\nTemporary password:  " + tp + "\n\nShare it securely. It must be changed at first sign-in and will not be shown again.", "Credentials");
}

inline void employeeForm(std::optional<Rec> ex) {
    Rec r = ex ? *ex : Rec{}; Form f(ex ? "Edit employee" : "Add employee");
    std::string name, email, phone, desig, join; long long sal = 0; int di = 0;
    FormOpt n = reqText("As per official records", 60); n.def = r.s("name");
    FormOpt em = reqText("Work email", 80); em.def = r.s("email");
    em.check = [&](const std::string& s) { if (s.find('@') == std::string::npos || s.find('.', s.find('@')) == std::string::npos) return std::string("Enter a valid email"); if (T("employees").first([&](const Rec& x) { return x.id != r.id && util::lower(x.s("email")) == util::lower(s); })) return std::string("This email is already registered"); return std::string(); };
    FormOpt ph; ph.optional = true; ph.def = r.s("phone"); ph.hint = "Digits, optionally with + or spaces";
    ph.check = [](const std::string& s) { int d = 0; for (char ch : s) { if (isdigit((unsigned char)ch)) d++; else if (!strchr("+- ()", ch)) return std::string("Only digits, + - ( ) allowed"); } return d >= 7 && d <= 15 ? std::string() : std::string("Phone must have 7-15 digits"); };
    FormOpt ds = reqText("Job title", 50); ds.def = r.s("designation");
    if (!f.ask("Full name", name, n) || !f.ask("Email", email, em) || !f.ask("Phone", phone, ph) || !f.ask("Designation", desig, ds)) return;
    auto depts = T("departments").all(); std::vector<std::string> dn{"— none —"}; int defIdx = 0;
    for (size_t i = 0; i < depts.size(); i++) { dn.push_back(depts[i].s("name")); if (depts[i].id == (uint64_t)r.n("dept_id")) defIdx = (int)i + 1; }
    if (!f.choose("Department", dn, di, defIdx)) return;
    if (!askDate(f, "Joining date", join, r.s("join_date", "today"))) return;
    if (!askMoney(f, "Monthly salary (gross)", sal, true, r.n("salary") ? std::to_string(r.n("salary") / 100) : "")) return;
    uint64_t mgr = (uint64_t)r.n("manager_id"); bool none = false;
    uint64_t pm = pickEmployee("Select reporting manager (N = none, Esc = keep)", [&](const Rec& x) { return x.id != r.id; }, true, &none);
    if (none) mgr = 0; else if (pm) mgr = pm;
    Fields fl = {{"name", name}, {"email", email}, {"phone", phone}, {"designation", desig}, {"dept_id", std::to_string(di ? depts[di - 1].id : 0)}, {"join_date", join}, {"salary", std::to_string(sal)}, {"manager_id", std::to_string(mgr)}};
    if (!confirm((ex ? "Save changes to " : "Add employee ") + name + "?", true, c::ACC)) return;
    if (ex) { T("employees").modify(ex->id, [&](Rec& x) { for (auto& kv : fl) x.set(kv.first, kv.second); return true; }); audit("EMP_UPDATED", ex->s("code") + " " + name); flash('o', "Employee updated."); }
    else { fl["status"] = "Active"; uint64_t id = createEmployee(fl); audit("EMP_CREATED", empCode(id) + " " + name); flash('o', name + " added as " + empCode(id) + ".\nUse the employee page to create a login."); }
}

inline void employeeCard(uint64_t id) {
    while (true) {
        auto eo = empById(id); if (!eo) return; Rec e = *eo; auto u = userOfEmp(id);
        size_t assets = T("assets").where([&](const Rec& a) { return (uint64_t)a.n("assigned_to") == id && a.s("status") == "Assigned"; }).size();
        std::vector<std::pair<std::string, std::string>> kv = {{"Employee code", e.s("code")}, {"Name", e.s("name")}, {"Email", e.s("email")}, {"Phone", e.s("phone", "—")}, {"Department", deptName((uint64_t)e.n("dept_id"))}, {"Designation", e.s("designation")}, {"Reports to", e.n("manager_id") ? empLabel((uint64_t)e.n("manager_id")) : "—"}, {"Joined", niceYmd(e.s("join_date"))}};
        if (can("emp.manage") || can("payroll.view")) kv.push_back({"Monthly salary", e.n("salary") ? M(e.n("salary")) : "not set"});
        kv.push_back({"Status", strip(badge(e.s("status", "Active")))}); kv.push_back({"Login", u ? u->s("username") + "  (" + u->s("role") + (u->b("active") ? "" : ", disabled") + ")" : "none"}); kv.push_back({"Assets held", std::to_string(assets)});
        std::vector<std::pair<char, std::string>> hot;
        if (can("emp.manage")) { hot = {{'e', "Edit"}, {'l', u ? "Reset password" : "Create login"}}; hot.push_back(e.s("status") == "Exited" ? std::make_pair('r', std::string("Reactivate")) : std::make_pair('x', std::string("Mark exited"))); }
        int k = card(e.s("name"), kv, hot); if (!k) return;
        if (k == 'e') employeeForm(e);
        else if (k == 'l') createLogin(e);
        else if (k == 'x') {
            if (e.id == myEmp()) { flash('e', "You cannot mark yourself as exited."); continue; }
            if (assets) { flash('e', "Return " + std::to_string(assets) + " assigned asset(s) before offboarding."); continue; }
            if (!T("employees").where([&](const Rec& x) { return (uint64_t)x.n("manager_id") == id && x.s("status") != "Exited"; }).empty()) { flash('w', "This person still manages active employees. Reassign them first."); continue; }
            if (confirm("Mark " + e.s("name") + " as exited? Their login will be disabled.")) {
                T("employees").modify(id, [&](Rec& x) { x.set("status", "Exited").set("exit_date", ymd(util::todayDays())); return true; });
                if (u) T("users").modify(u->id, [&](Rec& x) { x.set("active", 0); return true; });
                audit("EMP_EXITED", e.s("code") + " " + e.s("name")); flash('o', "Employee marked as exited; login disabled.");
            }
        } else if (k == 'r') {
            T("employees").modify(id, [&](Rec& x) { x.set("status", "Active").set("exit_date", ""); return true; });
            if (u) T("users").modify(u->id, [&](Rec& x) { x.set("active", 1); return true; });
            audit("EMP_REACTIVATED", e.s("code")); flash('o', "Employee reactivated.");
        }
    }
}

inline void employees() {
    Crumb c("Employees"); std::string q; bool exited = false;
    while (true) {
        std::vector<Rec> list; std::vector<Row> rows;
        for (auto& e : T("employees").all()) {
            bool ex = e.s("status") == "Exited"; if (ex && !exited) continue;
            if (!q.empty() && !util::icontains(e.s("name") + e.s("code") + e.s("designation") + e.s("email"), q)) continue;
            list.push_back(e); rows.push_back({e.s("code"), e.s("name"), deptName((uint64_t)e.n("dept_id")), e.s("designation"), e.n("manager_id") ? empName((uint64_t)e.n("manager_id")) : "—", badge(e.s("status", "Active"))});
        }
        PickOpt o; o.title = "Employees"; o.hot = {{'s', "Search"}, {'x', exited ? "Hide exited" : "Show exited"}}; if (can("emp.manage")) o.hot.insert(o.hot.begin(), {'a', "Add"});
        o.empty = can("emp.manage") ? "No employees yet. Press A to add one." : "No employees found."; o.note = q.empty() ? "" : "Filter: \"" + q + "\"";
        auto p = pickTable({{"Code"}, {"Name"}, {"Department"}, {"Designation"}, {"Reports to"}, {"Status"}}, rows, o);
        if (p.idx < 0 && p.key == 0) return;
        if (p.key == 'a') employeeForm(std::nullopt);
        else if (p.key == 'x') exited = !exited;
        else if (p.key == 's') { Form f("Search employees"); std::string s; FormOpt fo; fo.optional = true; fo.hint = "Name, code, email or designation (blank clears)"; if (f.ask("Search", s, fo)) q = s; }
        else if (p.idx >= 0) employeeCard(list[p.idx].id);
    }
}

// ================= leave =================
inline Rec balance(uint64_t emp, uint64_t type, int year) {
    auto match = [&](const Rec& r) { return (uint64_t)r.n("emp_id") == emp && (uint64_t)r.n("type_id") == type && r.n("year") == year; };
    auto& t = T("leave_balances"); auto b = t.first(match);
    if (!b) { t.insertUnique({{"emp_id", std::to_string(emp)}, {"type_id", std::to_string(type)}, {"year", std::to_string(year)}, {"extra", "0"}, {"used", "0"}}, {"emp_id", "type_id", "year"}); b = t.first(match); }
    return *b;
}
inline int thisYear() { int y, m, d; util::civilFromDays(util::todayDays(), y, m, d); return y; }
inline long long available(uint64_t emp, uint64_t type, int year) { auto lt = T("leave_types").get(type); if (!lt) return 0; auto b = balance(emp, type, year); return lt->n("days") + b.n("extra") - b.n("used"); }
inline void addUsed(uint64_t emp, uint64_t type, int year, long long delta) { Rec b = balance(emp, type, year); T("leave_balances").modify(b.id, [&](Rec& r) { r.set("used", std::max(0LL, r.n("used") + delta)); return true; }); }
inline std::string typeName(uint64_t id) { auto t = T("leave_types").get(id); return t ? t->s("name") : "—"; }

inline void leaveTypes() {
    Crumb c("Leave types");
    while (true) {
        auto ts = T("leave_types").all(); std::vector<Row> rows; for (auto& t : ts) rows.push_back({t.s("name"), std::to_string(t.n("days")), t.b("paid") ? "Paid" : "Unpaid"});
        PickOpt o; o.title = "Leave types"; o.hot = {{'a', "Add"}}; o.empty = "No leave types configured. Press A to create one (e.g. Casual Leave).";
        auto p = pickTable({{"Leave type"}, {"Days / year", 12, true}, {"Pay", 8}}, rows, o);
        if (p.idx < 0 && p.key == 0) return;
        auto form = [&](std::optional<Rec> ex) {
            Form f(ex ? "Edit leave type" : "New leave type"); std::string name; long long days = 0; int pi = 0;
            FormOpt n = reqText("e.g. Casual Leave, Sick Leave, Earned Leave", 40); n.def = ex ? ex->s("name") : "";
            n.check = [&](const std::string& s) { return T("leave_types").first([&](const Rec& r) { return (!ex || r.id != ex->id) && util::lower(r.s("name")) == util::lower(s); }) ? "Name already used" : ""; };
            if (!f.ask("Name", name, n) || !askInt(f, "Days per year", days, 0, 366, ex ? ex->s("days") : "", "Annual entitlement (0 for unlimited unpaid leave)") || !f.choose("Pay", {"Paid", "Unpaid"}, pi, ex && !ex->b("paid") ? 1 : 0)) return;
            if (ex) T("leave_types").modify(ex->id, [&](Rec& r) { r.set("name", name).set("days", days).set("paid", pi == 0 ? 1 : 0); return true; });
            else T("leave_types").insert({{"name", name}, {"days", std::to_string(days)}, {"paid", pi == 0 ? "1" : "0"}});
            audit(ex ? "LEAVE_TYPE_UPDATED" : "LEAVE_TYPE_CREATED", name); flash('o', "Leave type saved.");
        };
        if (p.key == 'a') form(std::nullopt); else if (p.idx >= 0) form(ts[p.idx]);
    }
}

inline void myBalances() {
    Crumb c("Balances"); int y = thisYear(); std::vector<Row> rows;
    for (auto& t : T("leave_types").all()) { auto b = balance(myEmp(), t.id, y); long long tot = t.n("days") + b.n("extra"); rows.push_back({t.s("name"), t.b("paid") ? std::to_string(tot) : "—", std::to_string(b.n("used")), t.b("paid") ? std::to_string(tot - b.n("used")) : "∞"}); }
    PickOpt o; o.title = "Leave balance " + std::to_string(y); o.selectable = false; o.empty = "HR has not configured leave types yet.";
    pickTable({{"Leave type"}, {"Entitled", 10, true}, {"Used", 8, true}, {"Available", 10, true}}, rows, o);
}

inline bool leaveOverlaps(uint64_t emp, int64_t a, int64_t b, uint64_t ignore = 0) {
    return !T("leaves").where([&](const Rec& l) { return l.id != ignore && (uint64_t)l.n("emp_id") == emp && (l.s("status") == "Pending" || l.s("status") == "Approved") && dayOf(l.s("from")) <= b && dayOf(l.s("to")) >= a; }).empty();
}
inline void notifyLeaveApprovers(const Rec& e, const std::string& text) {
    uint64_t mgr = (uint64_t)e.n("manager_id"); auto mu = mgr ? userOfEmp(mgr) : std::nullopt;
    if (mu && mu->b("active")) notifyUser(mu->id, text); else notifyPerm("leave.admin", text, A().user.id);
    if (mu) notifyPerm("leave.admin", text, mu->id == A().user.id ? 0 : A().user.id);
}
inline void applyLeave() {
    Crumb c("Apply for leave"); auto types = T("leave_types").all();
    if (types.empty()) { flash('i', "HR has not configured any leave types yet. Please ask HR / Admin to set them up."); return; }
    int y = thisYear(); std::vector<std::string> names; for (auto& t : types) names.push_back(t.s("name") + (t.b("paid") ? "   · " + std::to_string(available(myEmp(), t.id, y)) + " days left" : "   · unpaid"));
    Form f("Apply for leave"); int ti = 0; std::string from, to, reason;
    if (!f.choose("Leave type", names, ti)) return; Rec lt = types[ti];
    if (!askDate(f, "From", from, "today")) return;
    if (!askDate(f, "To", to, from)) return;
    int64_t a = dayOf(from), b = dayOf(to);
    if (b < a) { flash('e', "The end date is before the start date."); return; }
    if (b - a > 120) { flash('e', "A single request cannot span more than 120 days."); return; }
    int days = workDays(a, b); if (days == 0) { flash('e', "That range has no working days (weekly off only)."); return; }
    if (leaveOverlaps(myEmp(), a, b)) { flash('e', "You already have a pending or approved leave overlapping these dates."); return; }
    long long av = available(myEmp(), lt.id, y); if (lt.b("paid") && days > av) { flash('e', "Only " + std::to_string(av) + " day(s) of " + lt.s("name") + " available; you asked for " + std::to_string(days) + "."); return; }
    if (!f.ask("Reason", reason, reqText("Short reason for the approver", 100))) return;
    if (!confirm(lt.s("name") + ": " + niceYmd(from) + " → " + niceYmd(to) + "  (" + std::to_string(days) + " working day" + (days > 1 ? "s" : "") + "). Submit request?", true, c::ACC)) return;
    long long autoMax = SN("auto_leave_days"); bool autoOk = autoMax > 0 && days <= autoMax && lt.b("paid");
    uint64_t id = T("leaves").insert({{"emp_id", std::to_string(myEmp())}, {"type_id", std::to_string(lt.id)}, {"from", from}, {"to", to}, {"days", std::to_string(days)}, {"reason", reason}, {"status", autoOk ? "Approved" : "Pending"}, {"created", std::to_string(util::now())}, {"decided_by", autoOk ? "AUTO-POLICY" : ""}, {"decided_at", autoOk ? std::to_string(util::now()) : "0"}});
    if (autoOk) { addUsed(myEmp(), lt.id, y, days); audit("LEAVE_AUTO_APPROVED", "#" + std::to_string(id) + " " + A().emp.s("code") + " " + std::to_string(days) + "d"); flash('o', "Approved automatically by company policy (≤ " + std::to_string(autoMax) + " day(s)).", "Leave approved"); }
    else { audit("LEAVE_APPLIED", "#" + std::to_string(id) + " " + A().emp.s("code") + " " + from + ".." + to); notifyLeaveApprovers(A().emp, A().emp.s("name") + " requested " + std::to_string(days) + " day(s) of " + lt.s("name") + " (" + from + " → " + to + ")"); flash('o', "Request submitted. You'll be notified when it is decided.", "Leave requested"); }
}
inline std::vector<std::pair<std::string, std::string>> leaveKV(const Rec& l) {
    return {{"Request #", std::to_string(l.id)}, {"Employee", empLabel((uint64_t)l.n("emp_id"))}, {"Type", typeName((uint64_t)l.n("type_id"))}, {"From", niceYmd(l.s("from"))}, {"To", niceYmd(l.s("to"))}, {"Working days", std::to_string(l.n("days"))}, {"Reason", l.s("reason")}, {"Status", strip(badge(l.s("status")))}, {"Decided by", l.s("decided_by", "—")}, {"Decision note", l.s("note", "—")}, {"Requested", util::fmtTs(l.n("created"))}};
}
inline void cancelLeave(const Rec& l) {
    bool wasApproved = l.s("status") == "Approved";
    if (wasApproved && dayOf(l.s("from")) < util::todayDays()) { flash('e', "This leave has already started; ask HR to adjust it."); return; }
    if (!confirm("Cancel this leave request?")) return;
    bool ok = T("leaves").modify(l.id, [&](Rec& r) { if (r.s("status") != "Pending" && r.s("status") != "Approved") return false; r.set("status", "Cancelled"); return true; });
    if (ok && wasApproved) { int y, m, d; util::civilFromDays(dayOf(l.s("from")), y, m, d); addUsed((uint64_t)l.n("emp_id"), (uint64_t)l.n("type_id"), y, -l.n("days")); }
    if (ok) { audit("LEAVE_CANCELLED", "#" + std::to_string(l.id)); flash('o', "Leave cancelled."); }
}
inline void myLeaves() {
    Crumb c("My leaves");
    while (true) {
        auto ls = T("leaves").where([&](const Rec& l) { return (uint64_t)l.n("emp_id") == myEmp(); }); std::reverse(ls.begin(), ls.end()); std::vector<Row> rows;
        for (auto& l : ls) rows.push_back({"#" + std::to_string(l.id), typeName((uint64_t)l.n("type_id")), niceYmd(l.s("from")), niceYmd(l.s("to")), std::to_string(l.n("days")), badge(l.s("status"))});
        PickOpt o; o.title = "My leave requests"; o.hot = {{'n', "New request"}, {'b', "Balances"}}; o.empty = "You haven't requested any leave yet.";
        auto p = pickTable({{"Req"}, {"Type"}, {"From"}, {"To"}, {"Days", 5, true}, {"Status"}}, rows, o);
        if (p.idx < 0 && p.key == 0) return;
        if (p.key == 'n') applyLeave(); else if (p.key == 'b') myBalances();
        else if (p.idx >= 0) { Rec l = ls[p.idx]; bool cancellable = l.s("status") == "Pending" || l.s("status") == "Approved"; if (card("Leave request", leaveKV(l), cancellable ? std::vector<std::pair<char, std::string>>{{'c', "Cancel request"}} : std::vector<std::pair<char, std::string>>{}) == 'c') cancelLeave(l); }
    }
}
inline bool canDecideLeave(const Rec& l) {
    if (l.s("status") != "Pending") return false; auto e = empById((uint64_t)l.n("emp_id")); if (!e) return false;
    if ((uint64_t)l.n("emp_id") == myEmp()) return isAdmin();
    return can("leave.admin") || isMyReport(*e);
}
inline void decideLeave(uint64_t id) {
    auto lo = T("leaves").get(id); if (!lo) return; Rec l = *lo;
    if (!canDecideLeave(l)) { flash('w', "This request is no longer awaiting your decision."); return; }
    uint64_t emp = (uint64_t)l.n("emp_id"), type = (uint64_t)l.n("type_id"); int y, m, d; util::civilFromDays(dayOf(l.s("from")), y, m, d);
    auto kv = leaveKV(l); auto lt = T("leave_types").get(type); if (lt && lt->b("paid")) kv.push_back({"Balance now", std::to_string(available(emp, type, y)) + " day(s)"});
    int k = card("Leave request", kv, {{'a', "Approve"}, {'r', "Reject"}}); if (!k) return;
    bool self = emp == myEmp();
    if (k == 'a') {
        if (lt && lt->b("paid") && l.n("days") > available(emp, type, y)) { flash('e', "Insufficient balance. Reject, or have HR adjust the balance first."); return; }
        bool ok = T("leaves").modify(id, [&](Rec& r) { if (r.s("status") != "Pending") return false; r.set("status", "Approved").set("decided_by", me() + (self ? " (SELF)" : "")).set("decided_at", util::now()); return true; });
        if (!ok) { flash('w', "Someone else already decided this request."); return; }
        addUsed(emp, type, y, l.n("days")); audit(self ? "LEAVE_SELF_APPROVED" : "LEAVE_APPROVED", "#" + std::to_string(id) + " " + empCode(emp));
        notifyEmp(emp, "Your " + typeName(type) + " (" + l.s("from") + " → " + l.s("to") + ") was approved by " + A().emp.s("name")); flash('o', "Leave approved.");
    } else if (k == 'r') {
        std::string note; Form f("Reject leave"); if (!f.ask("Reason for rejection", note, reqText("Shown to the employee", 100))) return;
        bool ok = T("leaves").modify(id, [&](Rec& r) { if (r.s("status") != "Pending") return false; r.set("status", "Rejected").set("decided_by", me()).set("decided_at", util::now()).set("note", note); return true; });
        if (!ok) { flash('w', "Someone else already decided this request."); return; }
        audit("LEAVE_REJECTED", "#" + std::to_string(id) + " " + empCode(emp)); notifyEmp(emp, "Your " + typeName(type) + " request (" + l.s("from") + " → " + l.s("to") + ") was rejected: " + note); flash('o', "Leave rejected.");
    }
}
inline void allLeaves() {
    Crumb c("All leave records");
    while (true) {
        auto ls = T("leaves").all(); std::reverse(ls.begin(), ls.end()); if (ls.size() > 500) ls.resize(500); std::vector<Row> rows;
        for (auto& l : ls) rows.push_back({"#" + std::to_string(l.id), empName((uint64_t)l.n("emp_id")), typeName((uint64_t)l.n("type_id")), niceYmd(l.s("from")), niceYmd(l.s("to")), std::to_string(l.n("days")), badge(l.s("status"))});
        PickOpt o; o.title = "Leave records (latest 500)"; o.empty = "No leave requests have been filed yet.";
        auto p = pickTable({{"Req"}, {"Employee"}, {"Type"}, {"From"}, {"To"}, {"Days", 5, true}, {"Status"}}, rows, o);
        if (p.idx < 0) return; Rec l = ls[p.idx]; if (card("Leave request", leaveKV(l), canDecideLeave(l) ? std::vector<std::pair<char, std::string>>{{'d', "Decide"}} : std::vector<std::pair<char, std::string>>{}) == 'd') decideLeave(l.id);
    }
}
inline void adjustBalance() {
    Crumb c("Adjust balance"); uint64_t emp = pickEmployee("Select employee"); if (!emp) return;
    auto types = T("leave_types").where([](const Rec& t) { return t.b("paid"); }); if (types.empty()) { flash('i', "No paid leave types exist."); return; }
    int y = thisYear(); std::vector<std::string> names; for (auto& t : types) names.push_back(t.s("name") + "   · " + std::to_string(available(emp, t.id, y)) + " left");
    Form f("Adjust balance — " + empName(emp)); int ti = 0; long long delta = 0; std::string note;
    if (!f.choose("Leave type", names, ti) || !askInt(f, "Add / remove days", delta, -366, 366, "", "Positive adds days, negative removes (e.g. -2)") || !f.ask("Reason", note, reqText("Recorded in audit log", 100))) return;
    if (delta == 0) { flash('i', "Nothing to change."); return; }
    Rec b = balance(emp, types[ti].id, y); T("leave_balances").modify(b.id, [&](Rec& r) { r.set("extra", r.n("extra") + delta); return true; });
    audit("LEAVE_BALANCE_ADJUSTED", empCode(emp) + " " + types[ti].s("name") + " " + (delta > 0 ? "+" : "") + std::to_string(delta) + " (" + note + ")");
    notifyEmp(emp, "Your " + types[ti].s("name") + " balance was adjusted by " + std::to_string(delta) + " day(s): " + note); flash('o', "Balance adjusted.");
}

// ================= attendance =================
inline std::optional<Rec> attendanceOn(uint64_t emp, const std::string& day) { return T("attendance").first([&](const Rec& r) { return (uint64_t)r.n("emp_id") == emp && r.s("date") == day; }); }
inline std::string hoursStr(int64_t in, int64_t out) { if (!in || !out) return "—"; int m = (int)((out - in) / 60); char b[16]; snprintf(b, sizeof b, "%dh %02dm", m / 60, m % 60); return b; }
inline void attendanceSelf() {
    Crumb c("Attendance"); std::string today = ymd(util::todayDays());
    while (true) {
        auto rec = attendanceOn(myEmp(), today); std::string st = !rec ? paint(c::MUTE, "Not checked in today") : rec->n("out") ? paint(c::OK, "Checked in " + util::fmtClock(rec->n("in")) + " · out " + util::fmtClock(rec->n("out")) + " · " + hoursStr(rec->n("in"), rec->n("out"))) : paint(c::WARN, "Checked in at " + util::fmtClock(rec->n("in")) + " — working");
        std::string pre = box({"", "  " + niceYmd(today) + "      " + st, ""}, std::min(W(), 84), "Today", c::ACC);
        int s = menu("Attendance", {{"Check in", "", !rec}, {"Check out", "", rec && !rec->n("out")}, {"My attendance history", ""}}, pre);
        if (s < 0) return;
        if (s == 0) { uint64_t id = T("attendance").insertUnique({{"emp_id", std::to_string(myEmp())}, {"date", today}, {"in", std::to_string(util::now())}, {"out", "0"}}, {"emp_id", "date"}); if (id) { audit("CHECK_IN", A().emp.s("code")); } else flash('w', "Already checked in today."); }
        else if (s == 1) { T("attendance").modify(rec->id, [&](Rec& r) { if (r.n("out")) return false; r.set("out", util::now()); return true; }); audit("CHECK_OUT", A().emp.s("code")); }
        else {
            auto rs = T("attendance").where([&](const Rec& r) { return (uint64_t)r.n("emp_id") == myEmp(); }); std::reverse(rs.begin(), rs.end()); if (rs.size() > 62) rs.resize(62); std::vector<Row> rows;
            for (auto& r : rs) rows.push_back({niceYmd(r.s("date")), util::fmtClock(r.n("in")), util::fmtClock(r.n("out")), hoursStr(r.n("in"), r.n("out"))});
            PickOpt o; o.title = "My attendance"; o.selectable = false; o.empty = "No attendance recorded yet."; pickTable({{"Date"}, {"In"}, {"Out"}, {"Hours"}}, rows, o);
        }
    }
}
inline bool onLeave(uint64_t emp, int64_t day) {
    return T("leaves").first([&](const Rec& l) { return (uint64_t)l.n("emp_id") == emp && l.s("status") == "Approved" && dayOf(l.s("from")) <= day && dayOf(l.s("to")) >= day; }).has_value();
}
inline void attendanceRegister() {
    Crumb c("Attendance register"); int64_t day = util::todayDays();
    while (true) {
        std::string ds = ymd(day); std::vector<Row> rows; int present = 0, leave = 0, absent = 0; bool offDay = workDays(day, day) == 0;
        for (auto& e : T("employees").all()) {
            if (e.s("status") == "Exited" || (e.s("join_date") > ds)) continue;
            if (!can("att.view") && !isMyReport(e)) continue;
            auto r = attendanceOn(e.id, ds); std::string st;
            if (r) { st = "Present"; present++; } else if (onLeave(e.id, day)) { st = "On leave"; leave++; } else if (offDay) st = "Weekly off"; else { st = "Absent"; absent++; }
            rows.push_back({e.s("code"), e.s("name"), r ? util::fmtClock(r->n("in")) : "—", r ? util::fmtClock(r->n("out")) : "—", r ? hoursStr(r->n("in"), r->n("out")) : "—", badge(st)});
        }
        PickOpt o; o.title = "Attendance — " + util::niceDate(day) + "   present " + std::to_string(present) + " · on leave " + std::to_string(leave) + " · absent " + std::to_string(absent); o.selectable = false;
        o.hot = {{'p', "Previous day"}, {'n', "Next day"}, {'t', "Today"}}; o.empty = "No employees to show.";
        auto p = pickTable({{"Code"}, {"Name"}, {"In"}, {"Out"}, {"Hours"}, {"Status"}}, rows, o);
        if (p.key == 'p') day--; else if (p.key == 'n') day++; else if (p.key == 't') day = util::todayDays(); else if (p.key == 0) return;
    }
}
} // namespace hr
