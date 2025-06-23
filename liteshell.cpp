#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>
#include <fstream>
#include <dirent.h>
#include <fcntl.h>
#include <unordered_map>
#include <sys/stat.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <signal.h>
#include <termios.h>

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
void handle_alias(const vector<string> &args);
void add_to_history(const string &command);
vector<string> expand_wildcards(const string &pattern);
void setup_readline();
void reset_terminal();
void cleanup_terminal();
void sigint_handler(int sig);

// Global variables
vector<string> command_history;
unordered_map<string, string> aliases;
const string HISTORY_FILE = ".myshell_history";
const int MAX_HISTORY = 1000;
struct termios original_termios;

// Built-in commands
const vector<string> builtins = {
    "cd", "help", "exit", "history", "pwd", "ls", "alias"
};

bool is_builtin(const string &cmd);
int execute_builtin(const vector<string> &args);
void execute_redirection(const vector<string> &args, const string &input_file, 
                        const string &output_file, bool append);
void execute_pipeline(const vector<vector<string>> &commands);

// ANSI color codes
namespace Colors {
    const string RESET = "\033[0m";
    const string RED = "\033[31m";
    const string GREEN = "\033[32m";
    const string BLUE = "\033[34m";
    const string YELLOW = "\033[33m";
    const string MAGENTA = "\033[35m";
    const string CYAN = "\033[36m";
    const string BOLD = "\033[1m";
}

// Signal handler to restore prompt
void sigint_handler(int sig) {
    cout << "\n";
    rl_on_new_line();
    rl_replace_line("", 0);
    rl_redisplay();
}

void setup_terminal() {
    tcgetattr(STDIN_FILENO, &original_termios);
    atexit([]() {
        tcsetattr(STDIN_FILENO, TCSANOW, &original_termios);
        cout << Colors::RESET;
    });
}

void reset_terminal() {
    cout << Colors::RESET << "\n";
    cout.flush();
}

void cleanup_terminal() {
    cout << Colors::RESET;
    rl_reset_terminal(nullptr);
    rl_cleanup_after_signal();
}

void load_history() {
    ifstream history_file(HISTORY_FILE);
    if (history_file) {
        string line;
        while (getline(history_file, line)) {
            if (!line.empty()) {
                add_history(line.c_str());
                command_history.push_back(line);
                if (command_history.size() >= MAX_HISTORY) break;
            }
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

void setup_readline() {
    rl_readline_name = "myshell";
    rl_catch_signals = 1;
    rl_catch_sigwinch = 1;
    rl_attempted_completion_function = NULL;
    rl_bind_key('\t', rl_complete);
}

void add_to_history(const string &command) {
    if (command.empty()) {
        return;
    }
    
    if (!command_history.empty() && command_history.back() == command) {
        return;
    }
    
    add_history(command.c_str());
    command_history.push_back(command);
    
    if (command_history.size() > MAX_HISTORY) {
        command_history.erase(command_history.begin());
    }
}

void print_prompt() {
    cout << Colors::RESET;
    cout.flush();
    
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) {
        string username = getenv("USER") ? getenv("USER") : "user";
        char hostname[256];
        gethostname(hostname, sizeof(hostname));
        
        cout << Colors::BOLD << Colors::GREEN << username << "@" 
             << hostname << Colors::RESET << ":" 
             << Colors::BLUE << cwd << Colors::RESET << " " 
             << Colors::RED << "$ " << Colors::RESET;
    } else {
        cout << Colors::BOLD << Colors::GREEN << "myshell" 
             << Colors::RESET << " " 
             << Colors::RED << "$ " << Colors::RESET;
    }
    cout.flush();
}

vector<string> parse_command(const string &input) {
    vector<string> tokens;
    string token;
    bool in_quote = false;
    bool in_single_quote = false;
    bool escape_next = false;

    for (char c : input) {
        if (escape_next) {
            token += c;
            escape_next = false;
            continue;
        }

        if (c == '\\') {
            escape_next = true;
            continue;
        }

        if (c == '"' && !in_single_quote) {
            in_quote = !in_quote;
            continue;
        }

        if (c == '\'' && !in_quote) {
            in_single_quote = !in_single_quote;
            continue;
        }

        if (isspace(c) && !in_quote && !in_single_quote) {
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

    // Handle wildcard expansion
    vector<string> expanded_tokens;
    for (const auto &token : tokens) {
        if (token.find('*') != string::npos) {
            auto expanded = expand_wildcards(token);
            expanded_tokens.insert(expanded_tokens.end(), expanded.begin(), expanded.end());
        } else {
            expanded_tokens.push_back(token);
        }
    }

    return expanded_tokens;
}

vector<string> expand_wildcards(const string &pattern) {
    vector<string> matches;
    string dir_path = ".";
    string file_pattern = pattern;
    
    size_t last_slash = pattern.rfind('/');
    if (last_slash != string::npos) {
        dir_path = pattern.substr(0, last_slash);
        file_pattern = pattern.substr(last_slash + 1);
    }
    
    DIR *dir = opendir(dir_path.c_str());
    if (!dir) {
        matches.push_back(pattern);
        return matches;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        string filename = entry->d_name;
        
        if (file_pattern.empty() || file_pattern[0] != '.') {
            if (filename[0] == '.') continue;
        }
        
        bool match = true;
        size_t pattern_pos = 0;
        size_t name_pos = 0;
        
        while (pattern_pos < file_pattern.size() && name_pos < filename.size()) {
            if (file_pattern[pattern_pos] == '*') {
                if (pattern_pos == file_pattern.size() - 1) {
                    name_pos = filename.size();
                    break;
                }
                
                char next_char = file_pattern[pattern_pos + 1];
                while (name_pos < filename.size() && filename[name_pos] != next_char) {
                    name_pos++;
                }
                pattern_pos++;
            } else if (file_pattern[pattern_pos] == filename[name_pos]) {
                pattern_pos++;
                name_pos++;
            } else {
                match = false;
                break;
            }
        }
    
        if (match && pattern_pos == file_pattern.size() && name_pos == filename.size()) {
            string full_path = dir_path;
            if (dir_path != ".") full_path += "/";
            full_path += filename;
            matches.push_back(full_path);
        }
    }
    
    closedir(dir);
    
    if (matches.empty()) {
        matches.push_back(pattern);
    }
    
    return matches;
}

bool is_builtin(const string &cmd) {
    return find(builtins.begin(), builtins.end(), cmd) != builtins.end();
}

int execute_builtin(const vector<string> &args) {
    if (args[0] == "cd") {
        handle_cd(args);
    } else if (args[0] == "help") {
        handle_help();
    } else if (args[0] == "exit") {
        handle_exit();
        return -1;
    } else if (args[0] == "history") {
        handle_history(args);
    } else if (args[0] == "pwd") {
        handle_pwd();
    } else if (args[0] == "ls") {
        handle_ls(args);
    } else if (args[0] == "alias") {
        handle_alias(args);
    }
    return 0;
}

void handle_alias(const vector<string> &args) {
    if (args.size() == 1) {
        for (const auto &pair : aliases) {
            cout << pair.first << "=" << pair.second << endl;
        }
    } else {
        string definition = args[1];
        size_t equal_pos = definition.find('=');
        if (equal_pos == string::npos) {
            cout << "alias: syntax error, expected NAME=VALUE" << endl;
            return;
        }
        
        string name = definition.substr(0, equal_pos);
        string value = definition.substr(equal_pos + 1);
        aliases[name] = value;
        
        ofstream alias_file(".myshell_aliases", ios::app);
        if (alias_file) {
            alias_file << name << "=" << value << endl;
        }
    }
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
    if (getcwd(cwd, sizeof(cwd))) {
        cout << cwd << endl;
    } else {
        perror("pwd");
    }
}

void handle_ls(const vector<string> &args) {
    string path = ".";
    bool show_all = false;
    bool long_format = false;
    bool color = true;
    
    vector<string> paths;
    for (size_t i = 1; i < args.size(); i++) {
        if (args[i][0] == '-') {
            for (char c : args[i].substr(1)) {
                if (c == 'a') show_all = true;
                if (c == 'l') long_format = true;
                if (c == 'C') color = true;
            }
        } else {
            paths.push_back(args[i]);
        }
    }
    
    if (paths.empty()) {
        paths.push_back(".");
    }
    
    for (const auto &path : paths) {
        if (paths.size() > 1) {
            cout << path << ":" << endl;
        }
        
        DIR *dir;
        struct dirent *ent;
        if ((dir = opendir(path.c_str())) != nullptr) {
            vector<string> entries;
            while ((ent = readdir(dir)) != nullptr) {
                if (!show_all && ent->d_name[0] == '.') continue;
                entries.push_back(ent->d_name);
            }
            closedir(dir);
            
            sort(entries.begin(), entries.end());
            
            for (const auto &entry : entries) {
                if (long_format) {
                    cout << entry << endl;
                } else {
                    if (color) {
                        string color_code;
                        string full_path = path + "/" + entry;
                        
                        struct stat st;
                        if (stat(full_path.c_str(), &st) == 0) {
                            if (S_ISDIR(st.st_mode)) {
                                color_code = Colors::BLUE + Colors::BOLD;
                            } else if (st.st_mode & S_IXUSR) {
                                color_code = Colors::GREEN;
                            } else if (S_ISREG(st.st_mode)) {
                                size_t dot_pos = entry.rfind('.');
                                if (dot_pos != string::npos) {
                                    string ext = entry.substr(dot_pos + 1);
                                    if (ext == "c" || ext == "cpp" || ext == "h") {
                                        color_code = Colors::CYAN;
                                    } else if (ext == "jpg" || ext == "png" || ext == "gif") {
                                        color_code = Colors::MAGENTA;
                                    } else if (ext == "zip" || ext == "tar" || ext == "gz") {
                                        color_code = Colors::RED;
                                    }
                                }
                            }
                        }
                        
                        cout << color_code << entry << Colors::RESET << " ";
                    } else {
                        cout << entry << " ";
                    }
                }
            }
            
            if (!long_format) cout << endl;
        } else {
            perror("ls");
        }
    }
}

void handle_cd(const vector<string> &args) {
    if (args.size() == 1) {
        const char *home = getenv("HOME");
        if (home) {
            if (chdir(home) != 0) {
                perror("cd");
            }
        }
    } else if (args.size() == 2) {
        string path = args[1];
        if (path == "-") {
            const char *oldpwd = getenv("OLDPWD");
            if (oldpwd) {
                cout << oldpwd << endl;
                if (chdir(oldpwd) != 0) {
                    perror("cd");
                }
            } else {
                cerr << "cd: OLDPWD not set" << endl;
            }
        } else {
            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd))) {
                setenv("OLDPWD", cwd, 1);
                if (chdir(path.c_str()) != 0) {
                    perror("cd");
                }
            }
        }
    } else {
        cerr << "cd: too many arguments" << endl;
    }
}

void handle_help() {
    cout << "Enhanced C++ Shell" << endl;
    cout << "Built-in commands:" << endl;
    cout << "  cd [dir]       - Change directory (use '-' for previous directory)" << endl;
    cout << "  help           - Show this help message" << endl;
    cout << "  history [n]    - Show command history (last n commands)" << endl;
    cout << "  pwd            - Print working directory" << endl;
    cout << "  ls [options]   - List directory contents (-a: show hidden, -l: long format)" << endl;
    cout << "  alias [name=value] - Create or list command aliases" << endl;
    cout << "  exit           - Exit the shell" << endl;
    cout << "Features:" << endl;
    cout << "  I/O redirection: <, >, >>" << endl;
    cout << "  Piping: command1 | command2" << endl;
    cout << "  Wildcards: *, ?" << endl;
    cout << "  Tab completion for commands and filenames" << endl;
    cout << "  Command history with up/down arrows" << endl;
    cout << "  Background execution with &" << endl;
}

void handle_exit() {
    cout << "Goodbye!" << endl;
    save_history();
    cleanup_terminal();
    exit(0);
}

int execute_command(const vector<string> &args) {
    bool background = false;
    vector<string> cmd_args = args;
    if (!cmd_args.empty() && cmd_args.back() == "&") {
        background = true;
        cmd_args.pop_back();
    }
    
    string input_file, output_file;
    bool append = false;
    
    for (size_t i = 0; i < cmd_args.size(); ) {
        if (cmd_args[i] == "<") {
            if (i + 1 < cmd_args.size()) {
                input_file = cmd_args[i + 1];
                cmd_args.erase(cmd_args.begin() + i, cmd_args.begin() + i + 2);
            } else {
                cerr << "Syntax error: no input file specified" << endl;
                return -1;
            }
        } else if (cmd_args[i] == ">") {
            if (i + 1 < cmd_args.size()) {
                output_file = cmd_args[i + 1];
                append = false;
                cmd_args.erase(cmd_args.begin() + i, cmd_args.begin() + i + 2);
            } else {
                cerr << "Syntax error: no output file specified" << endl;
                return -1;
            }
        } else if (cmd_args[i] == ">>") {
            if (i + 1 < cmd_args.size()) {
                output_file = cmd_args[i + 1];
                append = true;
                cmd_args.erase(cmd_args.begin() + i, cmd_args.begin() + i + 2);
            } else {
                cerr << "Syntax error: no output file specified" << endl;
                return -1;
            }
        } else {
            i++;
        }
    }
    
    vector<vector<string>> pipe_commands;
    vector<string> current_command;
    
    for (const auto &arg : cmd_args) {
        if (arg == "|") {
            if (!current_command.empty()) {
                pipe_commands.push_back(current_command);
                current_command.clear();
            }
        } else {
            current_command.push_back(arg);
        }
    }
    
    if (!current_command.empty()) {
        pipe_commands.push_back(current_command);
    }
    
    if (pipe_commands.size() > 1) {
        execute_pipeline(pipe_commands);
        return 0;
    }
    
    if (!input_file.empty() || !output_file.empty()) {
        execute_redirection(cmd_args, input_file, output_file, append);
        return 0;
    }
    
    pid_t pid = fork();
    
    if (pid == 0) {
        vector<char*> argv;
        for (const auto &arg : cmd_args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        
        perror("execvp");
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        perror("fork");
        return -1;
    } else {
        if (!background) {
            int status;
            waitpid(pid, &status, 0);
            reset_terminal();
            return status;
        } else {
            cout << "[" << pid << "]" << endl;
            return 0;
        }
    }
}

void execute_redirection(const vector<string> &args, const string &input_file, 
                        const string &output_file, bool append) {
    pid_t pid = fork();
    
    if (pid == 0) {
        if (!input_file.empty()) {
            int fd = open(input_file.c_str(), O_RDONLY);
            if (fd < 0) {
                perror("open input file");
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        
        if (!output_file.empty()) {
            int flags = O_WRONLY | O_CREAT;
            if (append) {
                flags |= O_APPEND;
            } else {
                flags |= O_TRUNC;
            }
            
            int fd = open(output_file.c_str(), flags, 0644);
            if (fd < 0) {
                perror("open output file");
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        
        vector<char*> argv;
        for (const auto &arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        
        perror("execvp");
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        perror("fork");
    } else {
        int status;
        waitpid(pid, &status, 0);
        reset_terminal();
    }
}

void execute_pipeline(const vector<vector<string>> &commands) {
    if (commands.empty()) return;
    
    int num_commands = commands.size();
    int pipefds[2 * (num_commands - 1)];
    
    for (int i = 0; i < num_commands - 1; i++) {
        if (pipe(pipefds + i * 2) < 0) {
            perror("pipe");
            return;
        }
    }
    
    vector<pid_t> pids;
    
    for (int i = 0; i < num_commands; i++) {
        pid_t pid = fork();
        
        if (pid == 0) {
            if (i != 0) {
                dup2(pipefds[(i - 1) * 2], STDIN_FILENO);
            }
            
            if (i != num_commands - 1) {
                dup2(pipefds[i * 2 + 1], STDOUT_FILENO);
            }
            
            for (int j = 0; j < 2 * (num_commands - 1); j++) {
                close(pipefds[j]);
            }
            
            vector<char*> argv;
            for (const auto &arg : commands[i]) {
                argv.push_back(const_cast<char*>(arg.c_str()));
            }
            argv.push_back(nullptr);
            
            execvp(argv[0], argv.data());
            
            perror("execvp");
            exit(EXIT_FAILURE);
        } else if (pid < 0) {
            perror("fork");
            return;
        }
        
        pids.push_back(pid);
    }
    
    for (int i = 0; i < 2 * (num_commands - 1); i++) {
        close(pipefds[i]);
    }
    
    for (pid_t pid : pids) {
        waitpid(pid, nullptr, 0);
    }
    reset_terminal();
}

int main() {
    // Setup terminal and signals
    setup_terminal();
    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, SIG_IGN);
    
    string input;
    vector<string> args;
    int status = 0;

    // Initialize readline
    setup_readline();
    
    // Load command history from file
    load_history();
    
    // Load aliases from file
    ifstream alias_file(".myshell_aliases");
    if (alias_file) {
        string line;
        while (getline(alias_file, line)) {
            size_t pos = line.find('=');
            if (pos != string::npos) {
                string name = line.substr(0, pos);
                string value = line.substr(pos + 1);
                aliases[name] = value;
            }
        }
    }

    while (true) {
        print_prompt();
        char *input_cstr = readline("");
        if (!input_cstr) {  // Handle EOF (Ctrl+D)
            cout << endl;
            handle_exit();
            break;
        }
        
        input = input_cstr;
        free(input_cstr);

        // Skip empty input
        if (input.empty()) {
            continue;
        }

        // Add command to history
        add_to_history(input);

        // Check for aliases
        size_t first_space = input.find(' ');
        string first_word = (first_space == string::npos) ? input : input.substr(0, first_space);
        if (aliases.find(first_word) != aliases.end()) {
            string replacement = aliases[first_word];
            if (first_space != string::npos) {
                replacement += input.substr(first_space);
            }
            input = replacement;
        }

        // Parse and execute command
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