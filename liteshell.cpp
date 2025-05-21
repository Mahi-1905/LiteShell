#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>
#include <fstream>
#include <dirent.h> // For ls command

using namespace std;

// Function prototypes
void print_prompt();
vector<string> parse_command(const string &input);
int execute_command(const vector<string> &args);
void handle_cd(const vector<string> &args);
void handle_help();
void handle_exit();
void handle_history(const vector<string> &args);
void handle_pwd();
void handle_ls(const vector<string> &args);
void add_to_history(const string &command);

// Global history vector
vector<string> command_history;
const string HISTORY_FILE = ".myshell_history";
const int MAX_HISTORY = 100;

// Built-in commands
bool is_builtin(const string &cmd);
int execute_builtin(const vector<string> &args);

void load_history() {
    ifstream history_file(HISTORY_FILE);
    if (history_file) {
        string line;
        while (getline(history_file, line) && command_history.size() < MAX_HISTORY) {
            command_history.push_back(line);
        }
    }
}

void save_history() {
    ofstream history_file(HISTORY_FILE);
    if (history_file) {
        for (const auto& cmd : command_history) {
            history_file << cmd << endl;
        }
    }
}

int main() {
    string input;
    vector<string> args;
    int status = 0;

    // Load command history from file
    load_history();

    while (true) {
        print_prompt();
        getline(cin, input);

        // Skip empty input
        if (input.empty()) {
            continue;
        }

        // Add command to history (before parsing)
        add_to_history(input);

        args = parse_command(input);

        // Skip if no command was entered
        if (args.empty()) {
            continue;
        }

        // Check for built-in commands
        if (is_builtin(args[0])) {
            status = execute_builtin(args);
            if (status == -1) { // Exit code for shell termination
                save_history();
                break;
            }
            continue;
        }

        // Execute external command
        status = execute_command(args);
    }

    return 0;
}

void add_to_history(const string &command) {
    // Don't add empty commands
    if (command.empty()) {
        return;
    }
    
    // Don't add consecutive duplicates
    if (!command_history.empty() && command_history.back() == command) {
        return;
    }
    
    command_history.push_back(command);
    
    // Trim history if it exceeds max size
    if (command_history.size() > MAX_HISTORY) {
        command_history.erase(command_history.begin());
    }
}

void print_prompt() {
    // ANSI color codes
    const string COLOR_RESET = "\033[0m";
    const string COLOR_RED = "\033[31m";
    const string COLOR_GREEN = "\033[32m";
    const string COLOR_BLUE = "\033[34m";
    const string COLOR_BOLD = "\033[1m";

    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        // Colored prompt format: [shellname:path] $
        cout << COLOR_BOLD << COLOR_GREEN << "myshell" 
             << COLOR_RESET << ":" 
             << COLOR_BLUE << cwd 
             << COLOR_RESET << " " 
             << COLOR_RED << "$ " 
             << COLOR_RESET;
    } else {
        // Fallback prompt if path unavailable
        cout << COLOR_BOLD << COLOR_GREEN << "myshell" 
             << COLOR_RESET << " " 
             << COLOR_RED << "$ " 
             << COLOR_RESET;
    }
    cout.flush();
}

vector<string> parse_command(const string &input) {
    vector<string> tokens;
    string token;
    bool in_quote = false;

    for (char c : input) {
        if (c == '"') {
            in_quote = !in_quote;
        } else if (isspace(c) && !in_quote) {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
        } else {
            token += c;
        }
    }

    if (!token.empty()) {
        tokens.push_back(token);
    }

    return tokens;
}

bool is_builtin(const string &cmd) {
    static const vector<string> builtins = {
        "cd", "help", "exit", "history", "pwd", "ls"
    };

    return find(builtins.begin(), builtins.end(), cmd) != builtins.end();
}

int execute_builtin(const vector<string> &args) {
    if (args[0] == "cd") {
        handle_cd(args);
    } else if (args[0] == "help") {
        handle_help();
    } else if (args[0] == "exit") {
        handle_exit();
        return -1; // Special return code to indicate shell should exit
    } else if (args[0] == "history") {
        handle_history(args);
    } else if (args[0] == "pwd") {
        handle_pwd();
    } else if (args[0] == "ls") {
        handle_ls(args);
    }
    return 0;
}

void handle_history(const vector<string> &args) {
    int show_count = command_history.size();
    
    if (args.size() > 1) {
        try {
            show_count = stoi(args[1]);
            if (show_count < 0) {
                cout << "history: count must be positive" << endl;
                return;
            }
            show_count = min(show_count, (int)command_history.size());
        } catch (const invalid_argument&) {
            cout << "history: " << args[1] << ": numeric argument required" << endl;
            return;
        }
    }
    
    int start_index = max(0, (int)command_history.size() - show_count);
    for (int i = start_index; i < command_history.size(); i++) {
        cout << " " << i + 1 << "  " << command_history[i] << endl;
    }
}

void handle_pwd() {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        cout << cwd << endl;
    } else {
        perror("pwd");
    }
}

void handle_ls(const vector<string> &args) {
    string path = ".";
    if (args.size() > 1) {
        path = args[1];
    }

    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir(path.c_str())) != nullptr) {
        while ((ent = readdir(dir)) != nullptr) {
            // Skip hidden files unless specifically requested
            if (ent->d_name[0] != '.') {
                cout << ent->d_name << " ";
            }
        }
        cout << endl;
        closedir(dir);
    } else {
        perror("ls");
    }
}

void handle_cd(const vector<string> &args) {
    if (args.size() == 1) {
        // Change to home directory
        const char *home = getenv("HOME");
        if (home) {
            if (chdir(home) != 0) {
                perror("cd");
            }
        }
    } else if (args.size() == 2) {
        if (chdir(args[1].c_str()) != 0) {
            perror("cd");
        }
    } else {
        cerr << "cd: too many arguments" << endl;
    }
}

void handle_help() {
    cout << "Simple C++ Shell" << endl;
    cout << "Built-in commands:" << endl;
    cout << "  cd [dir]     - Change directory" << endl;
    cout << "  help         - Show this help message" << endl;
    cout << "  history [n]  - Show command history (last n commands)" << endl;
    cout << "  pwd          - Print working directory" << endl;
    cout << "  ls [dir]     - List directory contents" << endl;
    cout << "  exit         - Exit the shell" << endl;
    cout << "Other commands are executed as external programs." << endl;
}

void handle_exit() {
    cout << "Goodbye!" << endl;
}

int execute_command(const vector<string> &args) {
    pid_t pid = fork();
    
    if (pid == 0) { // Child process
        // Convert vector<string> to char* array
        vector<char*> argv;
        for (const auto &arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        // Execute the command
        execvp(argv[0], argv.data());
        
        // If execvp returns, there was an error
        perror("execvp");
        exit(EXIT_FAILURE);
    } else if (pid < 0) { // Fork error
        perror("fork");
        return -1;
    } else { // Parent process
        int status;
        waitpid(pid, &status, 0);
        return status;
    }
}