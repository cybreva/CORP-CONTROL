#include <iostream>
#include <fstream>
#include <string>

using namespace std;

struct CompanySettings {
    bool allowReimbursement;
    bool strictIpCheck;
    bool softwareRequestWindow;
    bool autoApproveLeave;
};

// 1. Function: Policy Load karna
void loadPolicies(CompanySettings &s) {
    ifstream fp("settings.dat", ios::binary); 
    if (fp.is_open()) {
        fp.read(reinterpret_cast<char*>(&s), sizeof(CompanySettings));
        fp.close();
    }
}

// 2. Function: Policy Save karna
void savePolicies(const CompanySettings &s) {
    ofstream out_fp("settings.dat", ios::binary);
    if (out_fp.is_open()) {
        out_fp.write(reinterpret_cast<const char*>(&s), sizeof(CompanySettings));
        out_fp.close();
        cout << "\n[SUCCESS] Company Policies Updated & Saved System-wide.\n" << flush;
    } else {
        cout << "\n[ERROR] Failed to save policies!\n" << flush;
    }
}

// 3. Function: Admin (HR/Management) Panel
void adminPanel(CompanySettings &s) {
    int choice;
    string yesno;

    do {
        cout << "\n=====================================\n"
             << "       HR / ADMIN POLICY PANEL       \n"
             << "=====================================\n"
             << "1. Toggle Remote Reimbursement Claims\n"
             << "2. Toggle Strict Network IP Check\n"
             << "3. Toggle Software License Request Window\n"
             << "4. Toggle Emergency Leave Auto-Approval\n"
             << "5. Save Policies & Exit\n"
             << "Enter Choice: " << flush ;
        cin >> choice;

        switch (choice) {
            case 1:
                cout << "Enable Reimbursements? (yes/no): " << flush; cin >> yesno;
                s.allowReimbursement = (yesno == "yes" || yesno == "YES" || yesno == "y" || yesno == "Y");
                break;
            case 2:
                cout << "Enable Strict IP Restriction? (yes/no): " << flush; cin >> yesno;
                s.strictIpCheck = (yesno == "yes" || yesno == "YES" || yesno == "y" || yesno == "Y");
                break;
            case 3:
                cout << "Open Software License Window? (yes/no): " << flush; cin >> yesno;
                s.softwareRequestWindow = (yesno == "yes" || yesno == "YES" || yesno == "y" || yesno == "Y");
                break;
            case 4:
                cout << "Enable Leave Auto-Approval? (yes/no): " << flush; cin >> yesno;
                s.autoApproveLeave = (yesno == "yes" || yesno == "YES" || yesno == "y" || yesno == "Y");
                break;
            case 5:
                savePolicies(s);
                break;
            default:
                cout << "Invalid Operational Command.\n" << flush ;
                break;
        }
    } while (choice != 5);
}

// 4. Function: Employee (User) Panel
void employeePanel(const CompanySettings &s) {
    int choice;
    do {
        cout << "\n=====================================\n"
             << "          EMPLOYEE DASHBOARD         \n"
             << "=====================================\n"
             << "1. File Internet/WFH Expense Claim\n"
             << "2. Request IDE/Software License (VS Code Pro/Jira)\n"
             << "3. Apply for Emergency Medical Leave\n"
             << "4. Exit Dashboard\n"
             << "Enter Choice: " << flush;
        cin >> choice;

        switch(choice) {
            case 1:
                if (s.allowReimbursement) {
                    double amount;
                    cout << "Enter claim amount ($): " << flush;
                     cin >> amount;
                    cout << "[SUCCESS] Claim of $" << amount << " submitted for accounting clearance.\n"<< flush;
                } else {
                    cout << "[DENIED] HR Policy Error: Remote reimbursements are currently DISABLED.\n" << flush ;
                }
                break;

            case 2:
                if (s.softwareRequestWindow) {
                    string softName;
                    cout << "Enter software name needed: "; cin >> softName;
                    cout << "[SUCCESS] Token generated for " << softName << " license assignment.\n" << flush;
                } else {
                    cout << "[DENIED] System Message: License portal is closed for scheduled maintenance.\n" << flush;
                }
                break;

            case 3:
                if (s.autoApproveLeave) {
                    cout << "[APPROVED] Medical leave auto-sanctioned. Inform your team lead.\n" << flush;
                } else {
                    cout << "[PENDING] Leave request routed to HR manager for review.\n" << flush;
                }
                break;

            case 4:
                cout << "Logging out from Corporate Network...\n" << flush;
                break;

            default:
                cout << "Invalid Action.\n" << flush;
                break;
        }
    } while (choice != 4);
}

int main() {
    
    ios :: sync_with_stdio(false);
    cin.tie(nullptr);

    CompanySettings s = {false, false, false, false};

    
    loadPolicies(s);

    string name;
    cout << "Enter your login username: " << flush;
    cin >> name; 

    if (name == "admin" || name == "ADMIN") {
        adminPanel(s); 
    } else {
        
        cout << "\nWelcome, " << name << " [Designation: Associate Staff]\n " << flush ;
        employeePanel(s);
    }

    return 0;
}