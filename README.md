# LiteShell - Custom Command Line Shell

# Overview
This project is a custom command line shell implemented in C++. It provides a lightweight, extensible command interpreter with features similar to common Unix shells like Bash.

# Features
- Basic command execution
- Built-in commands (cd, exit, help, etc.)
- Command history
- Input/output redirection
- Pipelining between commands
- Custom prompt configuration
- Signal handling (Ctrl+C, etc.)

### Built-in Commands
- `cd [dir]` - Change directory
- `exit` - Exit the shell
- `help` - Show help message
- `history` - Show command history
- `ls` - Set environment variable
- `alias` - creates shortcut for complex commands
- `pwd` - prints all files of working directory

### Examples
```
$ ls -l
$ cd text_files
$ echo "Hello" > sonnet.txt
$ sort < data.txt | echo
$ help
$ exit
```

## Known Limitations
- Limited Windows support (some features may not work)
- No advanced scripting features (loops, conditionals)

## Contributing
Contributions are welcome! Please fork the repository and submit pull requests.

