// finance.hpp - expense claims (multi-stage approval), categories, payroll, payslips, analytics
#pragma once
#include "hr.hpp"

namespace fin {
using namespace app;

inline std::string catName(uint64_t id) { auto c = T("exp_cats").get(id); return c ? c->s("name") : "—"; }

inline void categories() {
    Crumb c("Expense categories");
    while (true) {
        auto cs = T("exp_cats").all(); std::vector<Row> rows; for (auto& x : cs) rows.push_back({x.s("name"), x.n("limit") ? M(x.n("limit")) : "No limit"});
        PickOpt o; o.title = "Expense categories"; o.hot = {{'a', "Add"}}; o.empty = "No categories yet. Press A to add one (e.g. Travel, Meals, Software).";
        auto p = pickTable({{"Category"}, {"Max per claim", 20, true}}, rows, o);
        if (p.idx < 0 && p.key == 0) return;
        auto form = [&](std::optional<Rec> ex) {
            Form f(ex ? "Edit category" : "New category"); std::string name; long long lim = 0; FormOpt n = reqText("e.g. Travel, Meals, Internet", 40); n.def = ex ? ex->s("name") : "";
            n.check = [&](const std::string& s) { return T("exp_cats").first([&](const Rec& r) { return (!ex || r.id != ex->id) && util::lower(r.s("name")) == util::lower(s); }) ? "Name already used" : ""; };
            if (!f.ask("Name", name, n) || !askMoney(f, "Max amount per claim", lim, true, ex && ex->n("limit") ? std::to_string(ex->n("limit") / 100) : "")) return;
            if (ex) T("exp_cats").modify(ex->id, [&](Rec& r) { r.set("name", name).set("limit", lim); return true; }); else T("exp_cats").insert({{"name", name}, {"limit", std::to_string(lim)}});
            audit(ex ? "EXP_CAT_UPDATED" : "EXP_CAT_CREATED", name); flash('o', "Category saved.");
        };
        if (p.key == 'a') form(std::nullopt); else if (p.idx >= 0) {
            Rec x = cs[p.idx]; int k = card(x.s("name"), {{"Max per claim", x.n("limit") ? M(x.n("limit")) : "No limit"}}, {{'e', "Edit"}, {'d', "Delete"}});
            if (k == 'e') form(x);
            else if (k == 'd') { if (!T("expenses").where([&](const Rec& e) { return (uint64_t)e.n("cat_id") == x.id; }).empty()) flash('e', "Claims exist in this category; it cannot be deleted."); else if (confirm("Delete category \"" + x.s("name") + "\"?")) { T("exp_cats").remove(x.id); audit("EXP_CAT_DELETED", x.s("name")); } }
        }
    }
}

inline std::vector<std::pair<std::string, std::string>> expKV(const Rec& e) {
    return {{"Claim #", std::to_string(e.id)}, {"Employee", empLabel((uint64_t)e.n("emp_id"))}, {"Category", catName((uint64_t)e.n("cat_id"))}, {"Amount", M(e.n("amount"))}, {"Expense date", niceYmd(e.s("date"))}, {"Description", e.s("desc")}, {"Receipt / bill ref", e.s("receipt", "—")}, {"Status", strip(badge(e.s("status")))}, {"Manager review", e.s("mgr_by", "—")}, {"Finance review", e.s("fin_by", "—")}, {"Note", e.s("note", "—")}, {"Payment ref", e.s("pay_ref", "—")}, {"Submitted", util::fmtTs(e.n("created"))}};
}
inline void submitExpense() {
    Crumb c("New expense claim");
    if (S("allow_reimb") != "1") { flash('e', "Reimbursement claims are currently closed by company policy.", "Blocked by policy"); audit("EXPENSE_BLOCKED_BY_POLICY", A().emp.s("code")); return; }
    auto cats = T("exp_cats").all(); if (cats.empty()) { flash('i', "Finance has not configured expense categories yet."); return; }
    std::vector<std::string> names; for (auto& x : cats) names.push_back(x.s("name") + (x.n("limit") ? "   · max " + M(x.n("limit")) : ""));
    Form f("New expense claim"); int ci = 0; long long amt = 0; std::string date, desc, receipt;
    if (!f.choose("Category", names, ci)) return; Rec cat = cats[ci];
    if (!askMoney(f, "Amount", amt)) return;
    if (cat.n("limit") && amt > cat.n("limit")) { flash('e', "Amount exceeds the " + M(cat.n("limit")) + " limit for " + cat.s("name") + "."); return; }
    if (!askDate(f, "Expense date", date, "today")) return;
    int64_t d = dayOf(date), win = SN("exp_window_days");
    if (d > util::todayDays()) { flash('e', "Expense date cannot be in the future."); return; }
    if (win > 0 && util::todayDays() - d > win) { flash('e', "Claims must be filed within " + std::to_string(win) + " days of the expense."); return; }
    FormOpt rc; rc.optional = true; rc.hint = "Bill / invoice number (recommended)";
    if (!f.ask("Description", desc, reqText("What was this for?", 120)) || !f.ask("Receipt reference", receipt, rc)) return;
    if (!confirm("Submit " + M(amt) + " for " + cat.s("name") + "?", true, c::ACC)) return;
    uint64_t mgr = (uint64_t)A().emp.n("manager_id"); bool hasMgr = mgr && userOfEmp(mgr) && userOfEmp(mgr)->b("active");
    std::string st = hasMgr ? "Pending Manager" : (amt > SN("exp_fin_threshold") ? "Pending Finance" : "Approved");
    uint64_t id = T("expenses").insert({{"emp_id", std::to_string(myEmp())}, {"cat_id", std::to_string(cat.id)}, {"amount", std::to_string(amt)}, {"date", date}, {"desc", desc}, {"receipt", receipt}, {"status", st}, {"created", std::to_string(util::now())}});
    audit("EXPENSE_SUBMITTED", "#" + std::to_string(id) + " " + A().emp.s("code") + " " + M(amt));
    std::string msg = A().emp.s("name") + " filed " + M(amt) + " (" + cat.s("name") + ")";
    if (st == "Pending Manager") notifyUser(userOfEmp(mgr)->id, msg); else if (st == "Pending Finance") notifyPerm("exp.finance", msg, A().user.id); else notifyPerm("exp.finance", msg + " — approved, ready for payout", A().user.id);
    flash('o', "Claim #" + std::to_string(id) + " submitted. Current stage: " + st + ".", "Claim submitted");
}
inline void myExpenses() {
    Crumb c("My expenses");
    while (true) {
        auto es = T("expenses").where([&](const Rec& e) { return (uint64_t)e.n("emp_id") == myEmp(); }); std::reverse(es.begin(), es.end()); std::vector<Row> rows;
        for (auto& e : es) rows.push_back({"#" + std::to_string(e.id), niceYmd(e.s("date")), catName((uint64_t)e.n("cat_id")), M(e.n("amount")), badge(e.s("status"))});
        PickOpt o; o.title = "My expense claims"; o.hot = {{'n', "New claim"}}; o.empty = "No claims yet. Press N to file one.";
        auto p = pickTable({{"Claim"}, {"Date"}, {"Category"}, {"Amount", 16, true}, {"Status"}}, rows, o);
        if (p.idx < 0 && p.key == 0) return;
        if (p.key == 'n') submitExpense();
        else if (p.idx >= 0) {
            Rec e = es[p.idx]; bool pend = e.s("status").rfind("Pending", 0) == 0;
            if (card("Expense claim", expKV(e), pend ? std::vector<std::pair<char, std::string>>{{'c', "Cancel claim"}} : std::vector<std::pair<char, std::string>>{}) == 'c' && confirm("Cancel this claim?"))
                if (T("expenses").modify(e.id, [](Rec& r) { if (r.s("status").rfind("Pending", 0) != 0) return false; r.set("status", "Cancelled"); return true; })) { audit("EXPENSE_CANCELLED", "#" + std::to_string(e.id)); flash('o', "Claim cancelled."); }
        }
    }
}
inline bool canDecideExpense(const Rec& e) {
    auto emp = empById((uint64_t)e.n("emp_id")); if (!emp) return false; bool self = (uint64_t)e.n("emp_id") == myEmp(); const std::string st = e.s("status");
    if (st == "Pending Manager") return (!self || isAdmin()) && (isMyReport(*emp) || isAdmin());
    if (st == "Pending Finance") return can("exp.finance") && (!self || isAdmin());
    if (st == "Approved") return can("exp.finance");
    return false;
}
inline void decideExpense(uint64_t id) {
    auto eo = T("expenses").get(id); if (!eo) return; Rec e = *eo;
    if (!canDecideExpense(e)) { flash('w', "This claim is no longer awaiting your action."); return; }
    uint64_t emp = (uint64_t)e.n("emp_id"); bool self = emp == myEmp(); std::string st = e.s("status");
    std::vector<std::pair<char, std::string>> hot = st == "Approved" ? std::vector<std::pair<char, std::string>>{{'p', "Mark as paid"}, {'r', "Reject"}} : std::vector<std::pair<char, std::string>>{{'a', "Approve"}, {'r', "Reject"}};
    int k = card("Expense claim", expKV(e), hot); if (!k) return;
    std::string tag = me() + (self ? " (SELF)" : "");
    if (k == 'a') {
        bool toFin = st == "Pending Manager" && e.n("amount") > SN("exp_fin_threshold"); std::string next = st == "Pending Manager" ? (toFin ? "Pending Finance" : "Approved") : "Approved";
        bool ok = T("expenses").modify(id, [&](Rec& r) { if (r.s("status") != st) return false; r.set("status", next).set(st == "Pending Manager" ? "mgr_by" : "fin_by", tag); return true; });
        if (!ok) { flash('w', "Someone else already acted on this claim."); return; }
        audit(self ? "EXPENSE_SELF_APPROVED" : "EXPENSE_APPROVED", "#" + std::to_string(id) + " → " + next);
        if (next == "Pending Finance") notifyPerm("exp.finance", "Claim #" + std::to_string(id) + " (" + M(e.n("amount")) + ") needs finance review", A().user.id);
        else { notifyEmp(emp, "Your claim #" + std::to_string(id) + " (" + M(e.n("amount")) + ") was approved."); notifyPerm("exp.finance", "Claim #" + std::to_string(id) + " approved — ready for payout", A().user.id); }
        flash('o', "Claim moved to: " + next + ".");
    } else if (k == 'r') {
        std::string note; Form f("Reject claim"); if (!f.ask("Reason", note, reqText("Shown to the employee", 100))) return;
        bool ok = T("expenses").modify(id, [&](Rec& r) { if (r.s("status") != st) return false; r.set("status", "Rejected").set("note", note).set(st == "Pending Manager" ? "mgr_by" : "fin_by", tag); return true; });
        if (!ok) { flash('w', "Someone else already acted on this claim."); return; }
        audit("EXPENSE_REJECTED", "#" + std::to_string(id)); notifyEmp(emp, "Your claim #" + std::to_string(id) + " was rejected: " + note); flash('o', "Claim rejected.");
    } else if (k == 'p') {
        std::string ref; Form f("Record payout"); FormOpt o = reqText("UTR / cheque no. / voucher id", 60);
        if (!f.ask("Payment reference", ref, o)) return;
        bool ok = T("expenses").modify(id, [&](Rec& r) { if (r.s("status") != "Approved") return false; r.set("status", "Paid").set("pay_ref", ref).set("paid_at", util::now()); return true; });
        if (!ok) { flash('w', "This claim is no longer awaiting payment."); return; }
        audit("EXPENSE_PAID", "#" + std::to_string(id) + " " + M(e.n("amount")) + " ref " + ref); notifyEmp(emp, "Claim #" + std::to_string(id) + " (" + M(e.n("amount")) + ") has been paid. Ref: " + ref); flash('o', "Payout recorded.");
    }
}
inline void allExpenses() {
    Crumb c("All expense claims"); std::string filter;
    while (true) {
        auto es = T("expenses").all(); std::reverse(es.begin(), es.end()); std::vector<Rec> list; std::vector<Row> rows;
        for (auto& e : es) { if (!filter.empty() && util::lower(e.s("status")) != filter) continue; list.push_back(e); rows.push_back({"#" + std::to_string(e.id), empName((uint64_t)e.n("emp_id")), catName((uint64_t)e.n("cat_id")), M(e.n("amount")), niceYmd(e.s("date")), badge(e.s("status"))}); if (list.size() >= 500) break; }
        PickOpt o; o.title = "Expense claims" + (filter.empty() ? std::string("") : " — " + filter); o.hot = {{'f', "Filter status"}}; o.empty = "No claims match.";
        auto p = pickTable({{"Claim"}, {"Employee"}, {"Category"}, {"Amount", 16, true}, {"Date"}, {"Status"}}, rows, o);
        if (p.idx < 0 && p.key == 0) return;
        if (p.key == 'f') { Form f("Filter"); int i = 0; std::vector<std::string> ch = {"All", "Pending Manager", "Pending Finance", "Approved", "Paid", "Rejected", "Cancelled"}; if (f.choose("Status", ch, i)) filter = i ? util::lower(ch[i]) : ""; }
        else if (p.idx >= 0) { Rec e = list[p.idx]; if (card("Expense claim", expKV(e), canDecideExpense(e) ? std::vector<std::pair<char, std::string>>{{'d', "Take action"}} : std::vector<std::pair<char, std::string>>{}) == 'd') decideExpense(e.id); }
    }
}

// ================= payroll =================
struct Slip { uint64_t emp; long long gross, unpaid, pf, tax, net; int unpaidDays; };
inline std::vector<Slip> computePayroll(const std::string& ym) {
    int64_t a = 0, b = 0; util::monthRange(ym, a, b); std::vector<Slip> out; long long mdays = b - a + 1;
    for (auto& e : T("employees").all()) {
        long long sal = e.n("salary"); if (sal <= 0) continue; int64_t j = dayOf(e.s("join_date")); if (j > b) continue;
        int64_t ex = e.s("exit_date").empty() ? b : dayOf(e.s("exit_date")); if (ex < a) continue;
        int64_t from = std::max(a, j), to = std::min(b, ex); long long gross = sal * (to - from + 1) / mdays;
        int ud = 0; for (auto& l : T("leaves").where([&](const Rec& l) { return (uint64_t)l.n("emp_id") == e.id && l.s("status") == "Approved"; })) {
            auto lt = T("leave_types").get((uint64_t)l.n("type_id")); if (!lt || lt->b("paid")) continue;
            int64_t s = std::max(dayOf(l.s("from")), from), t = std::min(dayOf(l.s("to")), to); if (s <= t) ud += workDays(s, t);
        }
        long long unpaid = std::min(gross, sal / 30 * ud), base = gross - unpaid, pf = base * SN("pf_pct") / 100, tax = base * SN("tax_pct") / 100;
        out.push_back({e.id, gross, unpaid, pf, tax, base - pf - tax, ud});
    }
    return out;
}
inline void runPayroll() {
    Crumb c("Run payroll"); Form f("Run payroll"); std::string ym; FormOpt o; o.hint = "Month as YYYY-MM"; o.def = util::monthOf(util::todayDays());
    o.check = [](const std::string& s) { int64_t a, b; return util::monthRange(s, a, b) ? "" : "Use format YYYY-MM"; };
    if (!f.ask("Month", ym, o)) return;
    auto slips = computePayroll(ym); if (slips.empty()) { flash('i', "No employees with a salary are eligible for " + ym + ". Set salaries in the employee records first."); return; }
    std::vector<Row> rows; long long totNet = 0; size_t fresh = 0;
    for (auto& s : slips) { bool ex = T("payslips").first([&](const Rec& r) { return (uint64_t)r.n("emp_id") == s.emp && r.s("month") == ym; }).has_value(); if (!ex) { fresh++; totNet += s.net; }
        rows.push_back({empName(s.emp), M(s.gross), s.unpaidDays ? "-" + M(s.unpaid) + " (" + std::to_string(s.unpaidDays) + "d)" : "—", M(s.pf + s.tax), M(s.net), ex ? paint(c::MUTE, "exists") : paint(c::OK, "new")}); }
    PickOpt po; po.title = "Payroll preview — " + ym; po.selectable = false; po.note = std::to_string(fresh) + " new payslip(s) · total net payable " + M(totNet) + "   (Esc to continue)";
    pickTable({{"Employee"}, {"Gross", 16, true}, {"Unpaid leave", 22, true}, {"PF + Tax", 16, true}, {"Net pay", 16, true}, {"State"}}, rows, po);
    if (!fresh) { flash('i', "All payslips for " + ym + " already exist."); return; }
    if (!confirm("Generate " + std::to_string(fresh) + " payslip(s) for " + ym + "? Total net: " + M(totNet) + ".", false, c::ACC)) return;
    for (auto& s : slips) {
        uint64_t id = T("payslips").insertUnique({{"emp_id", std::to_string(s.emp)}, {"month", ym}, {"gross", std::to_string(s.gross)}, {"unpaid", std::to_string(s.unpaid)}, {"unpaid_days", std::to_string(s.unpaidDays)}, {"pf", std::to_string(s.pf)}, {"tax", std::to_string(s.tax)}, {"net", std::to_string(s.net)}, {"status", "Generated"}, {"by", me()}, {"created", std::to_string(util::now())}}, {"emp_id", "month"});
        if (id) notifyEmp(s.emp, "Your payslip for " + ym + " is ready.");
    }
    audit("PAYROLL_RUN", ym + " " + std::to_string(fresh) + " slips, net " + M(totNet)); flash('o', std::to_string(fresh) + " payslip(s) generated for " + ym + ".");
}
inline std::vector<std::pair<std::string, std::string>> slipKV(const Rec& s) {
    return {{"Employee", empLabel((uint64_t)s.n("emp_id"))}, {"Month", s.s("month")}, {"Gross earnings", M(s.n("gross"))}, {"Unpaid leave (" + std::to_string(s.n("unpaid_days")) + " d)", "-" + M(s.n("unpaid"))}, {"Provident fund", "-" + M(s.n("pf"))}, {"Income tax", "-" + M(s.n("tax"))}, {"NET PAY", M(s.n("net"))}, {"Status", strip(badge(s.s("status")))}};
}
inline void payslips(bool all) {
    Crumb c(all ? "Payslips" : "My payslips"); std::string month;
    while (true) {
        auto ps = T("payslips").where([&](const Rec& s) { return (all || (uint64_t)s.n("emp_id") == myEmp()) && (month.empty() || s.s("month") == month); }); std::reverse(ps.begin(), ps.end()); std::vector<Row> rows;
        for (auto& s : ps) rows.push_back({s.s("month"), empName((uint64_t)s.n("emp_id")), M(s.n("gross")), M(s.n("net")), badge(s.s("status"))});
        PickOpt o; o.title = all ? "Payslips" + (month.empty() ? std::string("") : " — " + month) : "My payslips"; o.empty = "No payslips generated yet.";
        if (all) { o.hot = {{'m', "Month filter"}}; if (can("payroll.run")) o.hot.push_back({'p', "Mark month paid"}); }
        auto p = pickTable({{"Month"}, {"Employee"}, {"Gross", 16, true}, {"Net pay", 16, true}, {"Status"}}, rows, o);
        if (p.idx < 0 && p.key == 0) return;
        if (p.key == 'm') { Form f("Filter by month"); std::string m; FormOpt mo; mo.optional = true; mo.hint = "YYYY-MM (blank = all)"; if (f.ask("Month", m, mo)) month = m; }
        else if (p.key == 'p') {
            if (month.empty()) { flash('i', "Set a month filter first (M)."); continue; }
            size_t n = 0; for (auto& s : ps) if (s.s("status") == "Generated") n++;
            if (n && confirm("Mark " + std::to_string(n) + " payslip(s) of " + month + " as PAID?", false, c::ACC)) { for (auto& s : ps) if (s.s("status") == "Generated") { T("payslips").modify(s.id, [](Rec& r) { r.set("status", "Paid"); return true; }); notifyEmp((uint64_t)s.n("emp_id"), "Salary for " + month + " has been paid."); } audit("PAYROLL_PAID", month + " " + std::to_string(n) + " slips"); flash('o', "Marked as paid."); }
        } else if (p.idx >= 0) card("Payslip", slipKV(ps[p.idx]));
    }
}

// ================= analytics =================
inline void analytics() {
    Crumb c("Expense analytics");
    while (true) {
        int s = menu("Expense analytics", {{"By status", "count & value"}, {"By category", "all time"}, {"By department", "this month vs budget"}, {"By month", "last 12 months"}}); if (s < 0) return;
        auto es = T("expenses").all(); std::map<std::string, std::pair<int, long long>> g; std::vector<Row> rows; PickOpt o; o.selectable = false; o.empty = "No expense data yet.";
        if (s == 0) { for (auto& e : es) { auto& x = g[e.s("status")]; x.first++; x.second += e.n("amount"); } for (auto& kv : g) rows.push_back({badge(kv.first), std::to_string(kv.second.first), M(kv.second.second)}); o.title = "Claims by status"; pickTable({{"Status"}, {"Claims", 8, true}, {"Value", 18, true}}, rows, o); }
        else if (s == 1) { for (auto& e : es) if (e.s("status") != "Rejected" && e.s("status") != "Cancelled") { auto& x = g[catName((uint64_t)e.n("cat_id"))]; x.first++; x.second += e.n("amount"); } for (auto& kv : g) rows.push_back({kv.first, std::to_string(kv.second.first), M(kv.second.second)}); o.title = "Spend by category (excl. rejected/cancelled)"; pickTable({{"Category"}, {"Claims", 8, true}, {"Value", 18, true}}, rows, o); }
        else if (s == 2) {
            std::string m = util::monthOf(util::todayDays()); for (auto& e : es) if (e.s("status") != "Rejected" && e.s("status") != "Cancelled" && e.s("date").substr(0, 7) == m) { auto emp = empById((uint64_t)e.n("emp_id")); auto& x = g[emp ? deptName((uint64_t)emp->n("dept_id")) : "—"]; x.first++; x.second += e.n("amount"); }
            for (auto& d : T("departments").all()) { auto it = g.find(d.s("name")); long long sp = it == g.end() ? 0 : it->second.second, bud = d.n("budget"); rows.push_back({d.s("name"), M(sp), bud ? M(bud) : "—", bud ? (sp > bud ? paint(c::BAD, "over by " + M(sp - bud)) : paint(c::OK, std::to_string(sp * 100 / bud) + "% used")) : "—"}); }
            o.title = "Department spend — " + m; pickTable({{"Department"}, {"Spent", 16, true}, {"Budget", 16, true}, {"Usage"}}, rows, o);
        } else { for (auto& e : es) if (e.s("status") != "Rejected" && e.s("status") != "Cancelled" && e.s("date").size() >= 7) { auto& x = g[e.s("date").substr(0, 7)]; x.first++; x.second += e.n("amount"); } for (auto it = g.rbegin(); it != g.rend() && rows.size() < 12; ++it) rows.push_back({it->first, std::to_string(it->second.first), M(it->second.second)}); o.title = "Monthly spend"; pickTable({{"Month"}, {"Claims", 8, true}, {"Value", 18, true}}, rows, o); }
    }
}
} // namespace fin
