#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>

using namespace std;

// Function prototypes
void print_prompt();
vector<string> parse_command(const string &input);
int execute_command(const vector<string> &args);
void handle_cd(const vector<string> &args);
void handle_help();
void handle_exit();

// Built-in commands
bool is_builtin(const string &cmd);
int execute_builtin(const vector<string> &args);

int main() {
    string input;
    vector<string> args;
    int status = 0;

    while (true) {
        print_prompt();
        getline(cin, input);

        // Skip empty input
        if (input.empty()) {
            continue;
        }

        args = parse_command(input);

        // Skip if no command was entered
        if (args.empty()) {
            continue;
        }

        // Check for built-in commands
        if (is_builtin(args[0])) {
            status = execute_builtin(args);
            if (status == -1) { // Exit code for shell termination
                break;
            }
            continue;
        }

        // Execute external command
        status = execute_command(args);
    }

    return 0;
}

void print_prompt() {
    // ANSI color codes
    const string COLOR_RESET = "\033[0m";
    const string COLOR_RED = "\033[31m";
    const string COLOR_GREEN = "\033[32m";
    const string COLOR_YELLOW = "\033[33m";
    const string COLOR_BLUE = "\033[34m";
    const string COLOR_MAGENTA = "\033[35m";
    const string COLOR_CYAN = "\033[36m";
    const string COLOR_WHITE = "\033[37m";
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
        "cd", "help", "exit"
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
    }
    return 0;
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