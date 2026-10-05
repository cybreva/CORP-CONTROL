<div align="center">

# CORP-CONTROL++

**HR, leave, expenses, payroll, assets and approvals, all inside your terminal.**

A terminal-native operations suite for small teams, with a tamper-evident audit trail.
Pure C++17. Zero dependencies. No demo data. Your data stays on your disk.

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![Dependencies](https://img.shields.io/badge/dependencies-none-brightgreen)
![License](https://img.shields.io/badge/license-MIT-yellow)
![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey)

</div>

<!-- Add a screenshot: ![Dashboard](docs/dashboard.png) -->

---

## Why this exists

Small companies often run approvals through chat, email and spreadsheets. Nobody knows where a request is stuck, who approved what, or whether company policy was followed. Full ERP systems solve this but are heavy, expensive and slow to set up.

CORP-CONTROL++ puts requests, approvals, policies and an audit trail into **one fast tool that needs nothing but a terminal**.

## Features

| Area | What you get |
|---|---|
| **People** | Employee directory, departments, reporting lines, login creation, safe offboarding |
| **Leave** | Configurable leave types, yearly balances, working-day calculation, overlap detection, manager/HR approval, auto-approval policy |
| **Attendance** | Check in / out, history, daily roll-call (present / on leave / absent / weekly off) |
| **Expenses** | Categories with per-claim limits, claim window, multi-stage approval (Manager → Finance → Payout) |
| **Payroll** | Monthly payslips with pro-rating, unpaid-leave deduction, PF and tax percentages |
| **Assets** | Asset register, assign / return, repair and retire flow |
| **Software requests** | Request and approve licences, controlled by a policy window |
| **Approvals inbox** | One queue for leave, expenses, payouts and software |
| **Live policies** | Switches and limits that change system behaviour instantly |
| **Kill-switch** | One action puts the system in maintenance mode and locks out non-admins |
| **Audit trail** | SHA-256 hash chain, so edits and deletions are detectable |
| **Backup / restore** | Snapshot and restore all data from inside the app |
| **Reports** | Headcount, leave utilisation, expense analytics, asset summary, CSV export |

## Quick start

### Arch Linux
```bash
sudo pacman -S --needed base-devel
git clone https://github.com/cybreva/CORP-CONTROL.git
cd CORP-CONTROL
make
./corpcontrol
```

### Debian / Ubuntu
```bash
sudo apt install build-essential
git clone https://github.com/cybreva/CORP-CONTROL.git
cd CORP-CONTROL
make && ./corpcontrol
```

### macOS
```bash
xcode-select --install
git clone https://github.com/cybreva/CORP-CONTROL.git
cd CORP-CONTROL
make && ./corpcontrol
```

### Windows (experimental)
Use [Windows Terminal](https://aka.ms/terminal) and any C++17 compiler:
```
g++ -std=c++17 -O2 src/main.cpp -o corpcontrol.exe
```

> The first compile can take up to a minute because everything builds as a single unit.
> For a faster build: `make CXXFLAGS="-std=c++17"`

### First run
A setup wizard asks for your company name, currency and the first Administrator account. **Nothing is pre-filled.** After that:

1. Create departments
2. Define leave types
3. Add expense categories
4. Add employees and create their logins

The Administrator dashboard shows this checklist until it is complete.

## Roles

| Role | What they can do |
|---|---|
| **Employee** | Apply for leave, check in / out, file expenses, request software, view own assets and payslips |
| **Manager** | All of the above, plus approve direct reports' leave and expenses, view team attendance |
| **HR** | Manage employees, departments, leave types and balances, assets, software approvals, attendance, reports |
| **Finance** | Review and pay expense claims, manage categories, run payroll, reports |
| **Admin** | Everything, plus policies, kill-switch, user management, audit trail, backup and restore |

Nobody can approve their own request, except an Admin, whose self-approvals are tagged `SELF` in the audit log.

## Keyboard

| Key | Action |
|---|---|
| `↑` `↓` | Move |
| `Enter` | Select / open |
| `Esc` | Back / cancel |
| `1`-`9` | Jump to a menu item |
| `[Letter]` | Hotkeys shown in the footer (e.g. `A` add, `S` search) |

Use a terminal of at least **100×30** with a font that supports box-drawing characters.

## Command line

```
corpcontrol [--data DIR] [--reset-password USER] [--version]
```

| Option | Meaning |
|---|---|
| `--data DIR` | Data directory (default `./corpdata`, or `$CORPCONTROL_DATA`) |
| `--reset-password USER` | Emergency recovery: issues a temporary password. Needs file access to the data directory |

## Data and backups

Everything lives in one directory (`corpdata/` by default):

```
corpdata/
├── users.tbl  employees.tbl  leaves.tbl  expenses.tbl  ...   one file per table
├── audit.log                                                  hash-chained audit trail
└── backups/                                                   from Administration › Backup
```

Back up this folder, or use **Administration › Backup & storage**. CSV exports go to an `exports/` folder next to the data directory.

## How it works

- **Storage engine.** Each table is an append-only log. Every line carries a CRC32 checksum and is `fsync`'d on write. On startup the log is replayed, and corrupt or half-written lines are skipped and reported in *Storage health*.
- **Multi-terminal.** A file lock serialises writes and readers tail-follow the log, so several terminals (for example SSH sessions) can share one data directory.
- **Authentication.** Salted, iterated SHA-256 password hashing, account lockout after repeated failures, idle-session timeout, forced password change for temporary passwords.
- **Money** is stored as integers (paise), never floating point.
- **Audit chain.** `hash = SHA256(previous_hash | time | user | action | detail)`. *Verify integrity* in the Audit trail recomputes the whole chain.

## Project layout

```
src/
├── util.hpp      dates, money, SHA-256, CRC32
├── store.hpp     storage engine and audit log
├── ui.hpp        terminal UI toolkit (menus, tables, forms)
├── core.hpp      settings, RBAC, authentication, notifications
├── hr.hpp        departments, employees, leave, attendance
├── finance.hpp   expenses, payroll, analytics
├── ops.hpp       assets, software, approvals, reports, CSV
├── admin.hpp     policies, kill-switch, users, audit, backup
└── main.cpp      setup wizard, login, dashboard, menus
legacy/           the original single-file prototype
```

## Limitations

- Not a client-server system. Several people can share one machine over SSH, but there is no network protocol.
- Password hashing is iterated SHA-256 (no external crypto library). Fine for an internal tool; consider Argon2 or bcrypt before exposing it to untrusted users.
- The audit chain detects edits and mid-log deletions. Truncating the tail of the log cannot be detected without an external anchor, so note the head hash from *Verify integrity* from time to time.
- Windows support is experimental.
- Notifications are in-app only (no email or push).

## Roadmap

- [ ] Argon2 password hashing
- [ ] Holiday calendar and half-day leave
- [ ] Payroll allowances and custom components
- [ ] PDF payslips
- [ ] Optional web / API front end

## Contributing

Bug reports and ideas are welcome through [Issues](https://github.com/cybreva/CORP-CONTROL/issues). This is an early release, so feedback helps a lot.

## License

MIT. See [LICENSE](LICENSE).

---

<div align="center">
Built by <a href="https://github.com/cybreva">cybreva</a>
</div>
