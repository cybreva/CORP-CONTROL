# CORP-CONTROL++ v2

Terminal-native operations suite: HR, leave, attendance, expenses, payroll, assets,
approvals, policies, tamper-evident audit log. Zero dependencies (C++17 only).

## Build
    make                      # Linux / macOS
    cmake -B build && cmake --build build   # any OS (Windows: use Windows Terminal)

## Run
    ./corpcontrol                       # data in ./corpdata
    ./corpcontrol --data /path/to/dir
    ./corpcontrol --data DIR --reset-password USER   # emergency recovery

First run starts a setup wizard (no pre-loaded data). Roles: Admin, HR, Finance, Manager, Employee.
Keys: arrows to move, Enter select, Esc back, letters in [brackets] are hotkeys.

## Layout
    src/util.hpp     dates, money (paise), SHA-256, CRC32
    src/store.hpp    crash-safe storage engine + hash-chained audit log
    src/ui.hpp       terminal UI toolkit
    src/core.hpp     settings, RBAC, auth, notifications
    src/hr.hpp       departments, employees, leave, attendance
    src/finance.hpp  expenses, payroll, analytics
    src/ops.hpp      assets, software, approvals, reports, CSV
    src/admin.hpp    policies, kill-switch, users, audit, backup
    legacy/          original single-file project
