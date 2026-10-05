#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <ctime>
#include <cstring>

using namespace std;

struct CompanySettings {
    bool allowReimbursement;
    bool strictIpCheck;
    bool softwareRequestWindow;
    bool autoApproveLeave;
    bool serviceDown; 
};

struct Application {
    char empName[50];
    char appType[50];
    char details[100];
    char timestamp[50];
    int status; // 0 = Pending, 1 = Approved, 2 = Rejected
};

// Helper: Current Date/Time fetcher
string getCurrentTime() {
    time_t now = time(0);
    char* dt = ctime(&now);
    string ts(dt);
    if (!ts.empty() && ts.back() == '\n') ts.pop_back();
    return ts;
}

// Helper: Logging automated failures/events
void logEvent(const string &message) {
    ofstream log_file("system.log", ios::app);
    if (log_file.is_open()) {
        log_file << "[" << getCurrentTime() << "] " << message << "\n";
        log_file.close();
    }
}

void loadPolicies(CompanySettings &s) {
    ifstream fp("settings.dat", ios::binary); 
    if (fp.is_open()) {
        fp.read(reinterpret_cast<char*>(&s), sizeof(CompanySettings));
        fp.close();
    }
}

void savePolicies(const CompanySettings &s) {
    ofstream out_fp("settings.dat", ios::binary);
    if (out_fp.is_open()) {
        out_fp.write(reinterpret_cast<const char*>(&s), sizeof(CompanySettings));
        out_fp.close();
        logEvent("SYSTEM CONFIG UPDATED SYSTEM-WIDE BY ADMIN");
    }
}

// Submit Application to File
void submitApplication(const string &name, const string &type, const string &details) {
    Application app;
    strncpy(app.empName, name.c_str(), 50);
    strncpy(app.appType, type.c_str(), 50);
    strncpy(app.details, details.c_str(), 100);
    strncpy(app.timestamp, getCurrentTime().c_str(), 50);
    app.status = 0; // Pending

    ofstream out("applications.dat", ios::binary | ios::app);
    if (out.is_open()) {
        out.write(reinterpret_cast<char*>(&app), sizeof(Application));
        out.close();
        cout << "[SUCCESS] Application sent to HR Portal queue.\n" << flush;
        logEvent("NEW APPLICATION SUBMITTED BY " + name);
    }
}

// 1. ADMIN PANEL (With Logs & Kill-Switch)
void adminPanel(CompanySettings &s) {
    int choice;
    string yesno;

    do {
        cout << "\n=====================================\n"
             << "          CORE ADMIN PANEL           \n"
             << "=====================================\n"
             << "1. Toggle Operations Policy Toggles\n"
             << "2. [KILL-SWITCH] Toggle Service Maintenance Mode\n"
             << "3. [DEBUGGER] View System Failure Logs\n"
             << "4. Save & Exit\n"
             << "Enter Operational Choice: " << flush;
        cin >> choice;

        if (choice == 1) {
            cout << "Enable Reimbursements? (y/n): " << flush; cin >> yesno;
            s.allowReimbursement = (yesno == "y" || yesno == "Y");
            cout << "Enable Strict IP Restriction? (y/n): " << flush; cin >> yesno;
            s.strictIpCheck = (yesno == "y" || yesno == "Y");
            cout << "Open License Window? (y/n): " << flush; cin >> yesno;
            s.softwareRequestWindow = (yesno == "y" || yesno == "Y");
            cout << "Enable Leave Auto-Approval? (y/n): " << flush; cin >> yesno;
            s.autoApproveLeave = (yesno == "y" || yesno == "Y");
        } 
        else if (choice == 2) {
            cout << "Activate Maintenance Shutdown Mode? (yes/no): " << flush; cin >> yesno;
            s.serviceDown = (yesno == "yes" || yesno == "YES");
            if(s.serviceDown) logEvent("CRITICAL ALERT: SYSTEM SERVICE FORCE DOWN BY ADMIN");
            else logEvent("SYSTEM SERVICE BROUGHT BACK ONLINE BY ADMIN");
        } 
        else if (choice == 3) {
            cout << "\n--- SYSTEM AUDIT TRAIL / LOGS ---\n" << flush;
            ifstream log_view("system.log");
            string line;
            if (log_view.is_open()) {
                while (getline(log_view, line)) cout << line << "\n";
                cout << flush;
                log_view.close();
            } else {
                cout << "No logs generated yet.\n" << flush;
            }
        } 
        else if (choice == 4) {
            savePolicies(s);
        }
    } while (choice != 4);
}

// 2. HR MANAGEMENT PANEL (Reviewing Queue)
void hrPanel() {
    int choice;
    do {
        cout << "\n=====================================\n"
             << "          HR REVIEW PORTAL           \n"
             << "=====================================\n"
             << "1. Review Employee Pending Applications\n"
             << "2. Exit HR Portal\n"
             << "Enter Choice: " << flush;
        cin >> choice;

        if (choice == 1) {
            fstream file("applications.dat", ios::binary | ios::in | ios::out);
            if (!file.is_open()) {
                cout << "No employee records found.\n" << flush;
                continue;
            }

            Application app;
            int count = 0;
            vector<streampos> positions;

            while (file.read(reinterpret_cast<char*>(&app), sizeof(Application))) {
                if (app.status == 0) { // Only print pending ones
                    positions.push_back(file.tellg() - streamoff(sizeof(Application)));
                    cout << "\nID [" << count++ << "] | Staff Name: " << app.empName 
                         << "\nType: " << app.appType << " | Info: " << app.details
                         << "\nTimestamp: " << app.timestamp << "\n-------------------\n" << flush;
                }
            }

            if (count == 0) {
                cout << "All clear! No applications left in review pipeline.\n" << flush;
                file.close();
                continue;
            }

            int reviewID, action;
            cout << "Enter Application ID to process (-1 to skip): " << flush; cin >> reviewID;
            if (reviewID >= 0 && reviewID < count) {
                cout << "Action (1 = Approve, 2 = Reject): " << flush; cin >> action;
                file.clear();
                file.seekp(positions[reviewID]);
                file.read(reinterpret_cast<char*>(&app), sizeof(Application));
                app.status = action;
                file.seekp(positions[reviewID]);
                file.write(reinterpret_cast<char*>(&app), sizeof(Application));
                cout << "Application decision processed.\n" << flush;
                logEvent("HR PROCESSED APPLICATION STATUS FOR EMPLOYEE " + string(app.empName));
            }
            file.close();
        }
    } while (choice != 2);
}

// 3. EMPLOYEE / CLIENT DASHBOARD
void employeePanel(const CompanySettings &s, const string &empName) {
    if (s.serviceDown) {
        cout << "\n[DENIED] System under maintenance. Action aborted.\n" << flush;
        logEvent("UNAUTHORIZED SYSTEM OPERATION ATTEMPTED BY " + empName + " WHILE SERVICE DOWN");
        return;
    }

    int choice;
    do {
        cout << "\n=====================================\n"
             << "          EMPLOYEE PORTAL            \n"
             << "=====================================\n"
             << "1. File Reimbursement Request\n"
             << "2. Request Dev Software Access\n"
             << "3. Apply for Emergency Leave\n"
             << "4. Exit\n"
             << "Enter Choice: " << flush;
        cin >> choice;

        switch(choice) {
            case 1:
                if (s.allowReimbursement) {
                    string amt; cout << "Enter Expense Amount & Details: " << flush; cin >> amt;
                    submitApplication(empName, "Reimbursement", "Amt: " + amt);
                } else {
                    cout << "[DENIED] Blocked by HR policy guidelines.\n" << flush;
                    logEvent("REIMBURSEMENT CLAIM REJECTED VIA AUTOMATED POLICY FOR " + empName);
                }
                break;
            case 2:
                if (s.softwareRequestWindow) {
                    string tool; cout << "Enter required software name: " << flush; cin >> tool;
                    submitApplication(empName, "Software-License", tool);
                } else {
                    cout << "[DENIED] Portal deployment pipeline locked.\n" << flush;
                }
                break;
            case 3:
                if (s.autoApproveLeave) {
                    cout << "[AUTO-APPROVED] Medical leave approved directly via Smart-Policy.\n" << flush;
                    logEvent("LEAVE AUTO-APPROVED FOR " + empName);
                } else {
                    submitApplication(empName, "Leave-Request", "Emergency Medical Leave");
                }
                break;
            case 4:
                break;
            default:
                cout << "Invalid Option.\n" << flush;
                logEvent("INVALID MENU SELECTION TRIPPED BY " + empName);
                break;
        }
    } while (choice != 4);
}

int main() {
    // 🚀 ULTRA-FAST I/O OPTIMIZATIONS
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    CompanySettings s = {false, false, false, false, false};
    loadPolicies(s);

    string username;
    cout << "Secure Login Authentication Protocol\n"
         << "Enter User ID: " << flush;
    cin >> username;

    if (username.rfind("admin", 0) == 0) { 
        adminPanel(s);
    } 
    else if (username.rfind("hr", 0) == 0) { 
        if (s.serviceDown) {
            cout << "\n[CRITICAL] Server Node Offline. Maintenance Window Active.\n" << flush;
            logEvent("HR LOGIN ATTEMPT BLOCKED DUE TO ACTIVE SYSTEM SHUTDOWN");
        } else {
            hrPanel();
        }
    } 
    else { 
        cout << "\nWelcome, " << username << " [Designation: Associate Staff]\n" << flush;
        employeePanel(s, username);
    }

    return 0;
}