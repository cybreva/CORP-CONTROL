// ops.hpp - assets, software requests, approvals inbox, notifications, profile, reports, CSV export
#pragma once
#include "finance.hpp"
#include <array>
#include <fstream>

namespace ops {
using namespace app;

// ================= assets =================
inline std::vector<std::pair<std::string, std::string>> assetKV(const Rec& a) {
    return {{"Asset tag", a.s("tag")}, {"Name", a.s("name")}, {"Category", a.s("category")}, {"Serial no.", a.s("serial", "—")}, {"Purchased", a.s("purchase_date").empty() ? "—" : niceYmd(a.s("purchase_date"))}, {"Cost", a.n("cost") ? M(a.n("cost")) : "—"}, {"Status", strip(badge(a.s("status")))}, {"Assigned to", a.n("assigned_to") ? empLabel((uint64_t)a.n("assigned_to")) : "—"}, {"Assigned on", a.s("assigned_on").empty() ? "—" : niceYmd(a.s("assigned_on"))}};
}
inline void addAsset() {
    Crumb c("Add asset"); Form f("Register new asset"); std::string name, serial, pd; long long cost = 0; int ci = 0;
    std::vector<std::string> cats = {"Laptop", "Desktop", "Mobile", "Monitor", "Peripheral", "Furniture", "Vehicle", "Network gear", "Other"};
    FormOpt sr; sr.optional = true; sr.hint = "Manufacturer serial (must be unique)"; sr.check = [](const std::string& s) { return T("assets").first([&](const Rec& r) { return util::lower(r.s("serial")) == util::lower(s); }) ? "This serial number is already registered" : ""; };
    if (!f.ask("Name / model", name, reqText("e.g. Dell Latitude 5540", 60)) || !f.choose("Category", cats, ci) || !f.ask("Serial number", serial, sr) || !askDate(f, "Purchase date", pd, "", true) || !askMoney(f, "Purchase cost", cost, true)) return;
    uint64_t id = T("assets").insert({{"name", name}, {"category", cats[ci]}, {"serial", serial}, {"purchase_date", pd}, {"cost", std::to_string(cost)}, {"status", "Available"}, {"assigned_to", "0"}});
    char tag[24]; snprintf(tag, sizeof tag, "AST-%04llu", (unsigned long long)id); T("assets").modify(id, [&](Rec& r) { r.set("tag", tag); return true; });
    audit("ASSET_ADDED", std::string(tag) + " " + name); flash('o', name + " registered as " + tag + ".");
}
inline void assetCard(uint64_t id) {
    while (true) {
        auto ao = T("assets").get(id); if (!ao) return; Rec a = *ao; std::string st = a.s("status"); std::vector<std::pair<char, std::string>> hot;
        if (can("asset.manage")) { if (st == "Available" || st == "Repair") hot.push_back({'a', "Assign"}); if (st == "Assigned") hot.push_back({'r', "Return"}); if (st == "Available") { hot.push_back({'w', "Send to repair"}); hot.push_back({'x', "Retire"}); } if (st == "Repair") hot.push_back({'b', "Back in stock"}); }
        int k = card(a.s("name"), assetKV(a), hot); if (!k) return;
        if (k == 'a') { if (st == "Repair" && !confirm("This asset is marked for repair. Assign anyway?")) continue; uint64_t emp = pickEmployee("Assign " + a.s("tag") + " to…"); if (!emp) continue;
            if (T("assets").modify(id, [&](Rec& r) { if (r.s("status") != "Available" && r.s("status") != "Repair") return false; r.set("status", "Assigned").set("assigned_to", (long long)emp).set("assigned_on", ymd(util::todayDays())); return true; })) { audit("ASSET_ASSIGNED", a.s("tag") + " → " + empCode(emp)); notifyEmp(emp, a.s("name") + " (" + a.s("tag") + ") was assigned to you."); flash('o', "Assigned to " + empName(emp) + "."); } }
        else if (k == 'r') { if (!confirm("Mark " + a.s("tag") + " as returned by " + empName((uint64_t)a.n("assigned_to")) + "?")) continue; uint64_t prev = (uint64_t)a.n("assigned_to");
            T("assets").modify(id, [&](Rec& r) { r.set("status", "Available").set("assigned_to", 0).set("assigned_on", ""); return true; }); audit("ASSET_RETURNED", a.s("tag") + " ← " + empCode(prev)); flash('o', "Asset returned to stock."); }
        else if (k == 'w' || k == 'b' || k == 'x') { std::string ns = k == 'w' ? "Repair" : k == 'b' ? "Available" : "Retired"; if (k == 'x' && !confirm("Retire " + a.s("tag") + " permanently?")) continue; T("assets").modify(id, [&](Rec& r) { r.set("status", ns); return true; }); audit("ASSET_STATUS", a.s("tag") + " → " + ns); }
    }
}
inline void assets() {
    Crumb c("Assets"); std::string q;
    while (true) {
        std::vector<Rec> list; std::vector<Row> rows;
        for (auto& a : T("assets").all()) { if (!q.empty() && !util::icontains(a.s("tag") + a.s("name") + a.s("serial") + a.s("category") + (a.n("assigned_to") ? empName((uint64_t)a.n("assigned_to")) : ""), q)) continue; list.push_back(a); rows.push_back({a.s("tag"), a.s("name"), a.s("category"), badge(a.s("status")), a.n("assigned_to") ? empName((uint64_t)a.n("assigned_to")) : "—"}); }
        PickOpt o; o.title = "Asset register"; o.hot = {{'a', "Add"}, {'s', "Search"}}; o.empty = "No assets registered. Press A to add the first one."; o.note = q.empty() ? "" : "Filter: \"" + q + "\"";
        auto p = pickTable({{"Tag"}, {"Name"}, {"Category"}, {"Status"}, {"Assigned to"}}, rows, o);
        if (p.idx < 0 && p.key == 0) return;
        if (p.key == 'a') addAsset(); else if (p.key == 's') { Form f("Search assets"); std::string s; FormOpt fo; fo.optional = true; fo.hint = "Tag, name, serial, category or person (blank clears)"; if (f.ask("Search", s, fo)) q = s; } else if (p.idx >= 0) assetCard(list[p.idx].id);
    }
}
inline void myAssets() {
    Crumb c("My assets"); std::vector<Row> rows;
    for (auto& a : T("assets").where([&](const Rec& r) { return (uint64_t)r.n("assigned_to") == myEmp() && r.s("status") == "Assigned"; })) rows.push_back({a.s("tag"), a.s("name"), a.s("category"), a.s("serial", "—"), niceYmd(a.s("assigned_on"))});
    PickOpt o; o.title = "Assets assigned to me"; o.selectable = false; o.empty = "No company assets are assigned to you."; pickTable({{"Tag"}, {"Name"}, {"Category"}, {"Serial"}, {"Since"}}, rows, o);
}

// ================= software requests =================
inline void requestSoftware() {
    Crumb c("Software access");
    if (S("sw_window") != "1") { flash('e', "The software request window is currently closed by company policy.", "Blocked by policy"); audit("SOFTWARE_REQUEST_BLOCKED", A().emp.s("code")); return; }
    Form f("Request software / license"); std::string sw, why;
    if (!f.ask("Software", sw, reqText("Name and version / plan", 60)) || !f.ask("Business justification", why, reqText("Why do you need it?", 120))) return;
    uint64_t id = T("sw_requests").insert({{"emp_id", std::to_string(myEmp())}, {"software", sw}, {"why", why}, {"status", "Pending"}, {"created", std::to_string(util::now())}});
    audit("SOFTWARE_REQUESTED", "#" + std::to_string(id) + " " + sw + " by " + A().emp.s("code")); notifyPerm("sw.approve", A().emp.s("name") + " requested software: " + sw, A().user.id); flash('o', "Request #" + std::to_string(id) + " sent for approval.");
}
inline std::vector<std::pair<std::string, std::string>> swKV(const Rec& r) { return {{"Request #", std::to_string(r.id)}, {"Employee", empLabel((uint64_t)r.n("emp_id"))}, {"Software", r.s("software")}, {"Justification", r.s("why")}, {"Status", strip(badge(r.s("status")))}, {"Decided by", r.s("decided_by", "—")}, {"Note", r.s("note", "—")}, {"Requested", util::fmtTs(r.n("created"))}}; }
inline bool canDecideSw(const Rec& r) { return r.s("status") == "Pending" && can("sw.approve") && ((uint64_t)r.n("emp_id") != myEmp() || isAdmin()); }
inline void decideSw(uint64_t id) {
    auto ro = T("sw_requests").get(id); if (!ro) return; Rec r = *ro; if (!canDecideSw(r)) { flash('w', "This request is no longer awaiting your decision."); return; }
    int k = card("Software request", swKV(r), {{'a', "Approve"}, {'r', "Reject"}}); if (!k) return; std::string note;
    if (k == 'r') { Form f("Reject request"); if (!f.ask("Reason", note, reqText("Shown to the employee", 100))) return; }
    std::string ns = k == 'a' ? "Approved" : "Rejected";
    if (!T("sw_requests").modify(id, [&](Rec& x) { if (x.s("status") != "Pending") return false; x.set("status", ns).set("decided_by", me()).set("note", note); return true; })) { flash('w', "Someone else already decided this."); return; }
    audit("SOFTWARE_" + util::lower(ns), "#" + std::to_string(id) + " " + r.s("software")); notifyEmp((uint64_t)r.n("emp_id"), "Your software request \"" + r.s("software") + "\" was " + util::lower(ns) + (note.empty() ? "." : ": " + note)); flash('o', "Request " + util::lower(ns) + ".");
}
inline void mySoftware() {
    Crumb c("My software requests");
    while (true) {
        auto rs = T("sw_requests").where([&](const Rec& r) { return (uint64_t)r.n("emp_id") == myEmp(); }); std::reverse(rs.begin(), rs.end()); std::vector<Row> rows; for (auto& r : rs) rows.push_back({"#" + std::to_string(r.id), r.s("software"), util::fmtTs(r.n("created")), badge(r.s("status"))});
        PickOpt o; o.title = "My software requests"; o.hot = {{'n', "New request"}}; o.empty = "No software requests yet.";
        auto p = pickTable({{"Req"}, {"Software"}, {"Requested"}, {"Status"}}, rows, o); if (p.idx < 0 && p.key == 0) return;
        if (p.key == 'n') requestSoftware(); else if (p.idx >= 0) card("Software request", swKV(rs[p.idx]));
    }
}

// ================= approvals inbox =================
struct Item { char kind; uint64_t id; int64_t created; Row row; };
inline std::vector<Item> buildApprovals() {
    std::vector<Item> v;
    for (auto& l : T("leaves").all()) if (hr::canDecideLeave(l)) v.push_back({'L', l.id, l.n("created"), {"Leave", empName((uint64_t)l.n("emp_id")), hr::typeName((uint64_t)l.n("type_id")) + ": " + niceYmd(l.s("from")) + " → " + niceYmd(l.s("to")) + " (" + std::to_string(l.n("days")) + "d)", util::fmtTs(l.n("created"))}});
    for (auto& e : T("expenses").all()) if (fin::canDecideExpense(e)) v.push_back({'E', e.id, e.n("created"), {e.s("status") == "Approved" ? "Payout" : "Expense", empName((uint64_t)e.n("emp_id")), fin::catName((uint64_t)e.n("cat_id")) + " " + M(e.n("amount")) + " — " + e.s("desc"), util::fmtTs(e.n("created"))}});
    for (auto& r : T("sw_requests").all()) if (canDecideSw(r)) v.push_back({'S', r.id, r.n("created"), {"Software", empName((uint64_t)r.n("emp_id")), r.s("software"), util::fmtTs(r.n("created"))}});
    std::sort(v.begin(), v.end(), [](const Item& a, const Item& b) { return a.created < b.created; }); return v;
}
inline void approvals() {
    Crumb c("Approvals");
    while (true) {
        auto items = buildApprovals(); std::vector<Row> rows; for (auto& i : items) rows.push_back(i.row);
        PickOpt o; o.title = "Approvals inbox — oldest first"; o.empty = "You're all caught up. Nothing awaits your decision.";
        auto p = pickTable({{"Type", 10}, {"Employee"}, {"Details", 60}, {"Submitted"}}, rows, o); if (p.idx < 0) return;
        auto& it = items[p.idx]; if (it.kind == 'L') hr::decideLeave(it.id); else if (it.kind == 'E') fin::decideExpense(it.id); else decideSw(it.id);
    }
}

// ================= notifications & profile =================
inline void inbox() {
    Crumb c("Inbox");
    while (true) {
        uint64_t uid = A().user.id; auto ns = T("notifs").where([&](const Rec& r) { return (uint64_t)r.n("user_id") == uid; }); std::reverse(ns.begin(), ns.end()); if (ns.size() > 200) ns.resize(200); std::vector<Row> rows;
        for (auto& n : ns) rows.push_back({n.b("read") ? paint(c::MUTE, "○") : paint(c::WARN, "●"), util::fmtTs(n.n("ts")), n.s("text")});
        PickOpt o; o.title = "Inbox"; o.hot = {{'m', "Mark all read"}, {'c', "Clear read"}}; o.empty = "No notifications.";
        auto p = pickTable({{"", 2}, {"When", 16}, {"Message", 100}}, rows, o); if (p.idx < 0 && p.key == 0) { refreshCtx(); return; }
        if (p.key == 'm') for (auto& n : ns) T("notifs").modify(n.id, [](Rec& r) { r.set("read", 1); return true; });
        else if (p.key == 'c') { for (auto& n : ns) if (n.b("read")) T("notifs").remove(n.id); }
        else if (p.idx >= 0) { Rec n = ns[p.idx]; T("notifs").modify(n.id, [](Rec& r) { r.set("read", 1); return true; }); card("Notification", {{"When", util::fmtTs(n.n("ts"))}, {"Message", n.s("text")}}); }
        refreshCtx();
    }
}
inline void profile() {
    Crumb c("My profile");
    while (true) {
        Rec e = A().emp; Rec u = A().user;
        int k = card("My profile", {{"Name", e.s("name")}, {"Employee code", e.s("code")}, {"Email", e.s("email")}, {"Department", deptName((uint64_t)e.n("dept_id"))}, {"Designation", e.s("designation")}, {"Reports to", e.n("manager_id") ? empLabel((uint64_t)e.n("manager_id")) : "—"}, {"Joined", niceYmd(e.s("join_date"))}, {"Username", u.s("username")}, {"Role", u.s("role")}, {"Last sign-in", util::fmtTs(u.n("last_login"))}}, {{'p', "Change password"}});
        if (k == 'p') hr::changeOwnPassword(false); else return;
    }
}

// ================= reports & exports =================
inline void exportCsv(const std::string& name, const std::vector<std::string>& head, const std::vector<Row>& rows) {
    namespace fs = std::filesystem; fs::path dir = fs::path(A().db.dir()).parent_path() / "exports"; if (dir.empty()) dir = "exports"; fs::create_directories(dir);
    fs::path p = dir / (name + "-" + util::fmtStamp() + ".csv"); std::ofstream o(p, std::ios::binary); o << "\xEF\xBB\xBF";
    for (size_t i = 0; i < head.size(); i++) o << (i ? "," : "") << util::csvEscape(head[i]); o << "\r\n";
    for (auto& r : rows) { for (size_t i = 0; i < r.size(); i++) o << (i ? "," : "") << util::csvEscape(strip(r[i])); o << "\r\n"; }
    audit("EXPORT", name + " " + std::to_string(rows.size()) + " rows"); flash('o', std::to_string(rows.size()) + " row(s) exported to:\n" + fs::absolute(p).string(), "Export complete");
}
inline void exports() {
    Crumb c("Export CSV");
    while (true) {
        std::vector<MenuItem> mi = {{"Employees", ""}, {"Leave requests", ""}, {"Expense claims", ""}, {"Assets", ""}, {"Payslips", "", can("payroll.view")}, {"Attendance", "", can("att.view")}};
        int s = menu("Export data to CSV", mi); if (s < 0) return; std::vector<Row> r;
        if (s == 0) { for (auto& e : T("employees").all()) r.push_back({e.s("code"), e.s("name"), e.s("email"), e.s("phone"), deptName((uint64_t)e.n("dept_id")), e.s("designation"), e.n("manager_id") ? empName((uint64_t)e.n("manager_id")) : "", e.s("join_date"), e.s("status", "Active"), can("payroll.view") ? std::to_string(e.n("salary") / 100) : ""}); exportCsv("employees", {"Code", "Name", "Email", "Phone", "Department", "Designation", "Manager", "Joined", "Status", "Monthly salary"}, r); }
        else if (s == 1) { for (auto& l : T("leaves").all()) r.push_back({std::to_string(l.id), empName((uint64_t)l.n("emp_id")), hr::typeName((uint64_t)l.n("type_id")), l.s("from"), l.s("to"), std::to_string(l.n("days")), l.s("status"), l.s("decided_by"), l.s("reason")}); exportCsv("leaves", {"Req", "Employee", "Type", "From", "To", "Days", "Status", "Decided by", "Reason"}, r); }
        else if (s == 2) { for (auto& e : T("expenses").all()) r.push_back({std::to_string(e.id), empName((uint64_t)e.n("emp_id")), fin::catName((uint64_t)e.n("cat_id")), std::to_string(e.n("amount") / 100) + "." + util::pad2((int)(e.n("amount") % 100)), e.s("date"), e.s("status"), e.s("mgr_by"), e.s("fin_by"), e.s("pay_ref"), e.s("desc")}); exportCsv("expenses", {"Claim", "Employee", "Category", "Amount", "Date", "Status", "Manager", "Finance", "Payment ref", "Description"}, r); }
        else if (s == 3) { for (auto& a : T("assets").all()) r.push_back({a.s("tag"), a.s("name"), a.s("category"), a.s("serial"), a.s("status"), a.n("assigned_to") ? empName((uint64_t)a.n("assigned_to")) : ""}); exportCsv("assets", {"Tag", "Name", "Category", "Serial", "Status", "Assigned to"}, r); }
        else if (s == 4) { for (auto& p : T("payslips").all()) r.push_back({p.s("month"), empName((uint64_t)p.n("emp_id")), std::to_string(p.n("gross") / 100), std::to_string(p.n("unpaid") / 100), std::to_string(p.n("pf") / 100), std::to_string(p.n("tax") / 100), std::to_string(p.n("net") / 100), p.s("status")}); exportCsv("payslips", {"Month", "Employee", "Gross", "Unpaid deduction", "PF", "Tax", "Net", "Status"}, r); }
        else { for (auto& a : T("attendance").all()) r.push_back({a.s("date"), empName((uint64_t)a.n("emp_id")), util::fmtClock(a.n("in")), util::fmtClock(a.n("out")), hr::hoursStr(a.n("in"), a.n("out"))}); exportCsv("attendance", {"Date", "Employee", "In", "Out", "Hours"}, r); }
    }
}
inline void reports() {
    Crumb c("Reports");
    while (true) {
        int s = menu("Reports & exports", {{"Headcount by department", ""}, {"Leave utilisation", "this year"}, {"Expense analytics", "", can("exp.finance") || isAdmin() || can("reports")}, {"Asset summary", ""}, {"Export to CSV", "employees, leaves, expenses…"}}); if (s < 0) return;
        std::vector<Row> rows; PickOpt o; o.selectable = false; o.empty = "No data yet.";
        if (s == 0) { std::map<std::string, std::pair<int, long long>> g; for (auto& e : T("employees").all()) if (e.s("status") != "Exited") { auto& x = g[deptName((uint64_t)e.n("dept_id"))]; x.first++; x.second += e.n("salary"); } bool sal = can("payroll.view"); for (auto& kv : g) rows.push_back({kv.first, std::to_string(kv.second.first), sal ? M(kv.second.second) : "—"}); o.title = "Active headcount by department"; pickTable({{"Department"}, {"Headcount", 10, true}, {"Monthly payroll cost", 22, true}}, rows, o); }
        else if (s == 1) { int y = hr::thisYear(); std::map<std::string, std::array<long long, 3>> g; for (auto& l : T("leaves").all()) { int64_t d = dayOf(l.s("from")); int yy, m, dd; util::civilFromDays(d, yy, m, dd); if (yy != y) continue; auto& x = g[hr::typeName((uint64_t)l.n("type_id"))]; if (l.s("status") == "Approved") x[0] += l.n("days"); else if (l.s("status") == "Pending") x[1] += l.n("days"); else if (l.s("status") == "Rejected") x[2]++; } for (auto& kv : g) rows.push_back({kv.first, std::to_string(kv.second[0]), std::to_string(kv.second[1]), std::to_string(kv.second[2])}); o.title = "Leave utilisation " + std::to_string(y); pickTable({{"Leave type"}, {"Approved days", 14, true}, {"Pending days", 14, true}, {"Rejected reqs", 14, true}}, rows, o); }
        else if (s == 2) fin::analytics();
        else if (s == 3) { std::map<std::string, std::map<std::string, int>> g; for (auto& a : T("assets").all()) g[a.s("category")][a.s("status")]++; for (auto& kv : g) { auto& m = kv.second; rows.push_back({kv.first, std::to_string(m["Available"]), std::to_string(m["Assigned"]), std::to_string(m["Repair"]), std::to_string(m["Retired"])}); } o.title = "Assets by category"; pickTable({{"Category"}, {"Available", 10, true}, {"Assigned", 10, true}, {"Repair", 8, true}, {"Retired", 8, true}}, rows, o); }
        else exports();
    }
}
} // namespace ops
