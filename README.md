# iota

Given a command line in a terminal on a unix-like computer, the iota project offers three tools that search for filenames and for text in line-oriented files (like source code).

The three tools are:

* __match__ searches for files with names that match a string. The search is recursive starting from the current working directory and matches with shell-style pattern matching, as if the string arguments were prefixes and
suffixed with *. Writes its results as a list of "refs" to the file named in REFS_PATH environment variable or to ~/.refs. See the __ref__ program below.

* __search__ is a line-oriented grep-like program that finds text in text files. The search is recursive starting from the current working directory. Search terms can be strings or regular expressions. Writes its results a list of "refs" to the file named in REFS_PATH environment variable or to ~/.refs. See the __ref__ program below.

* __ref__ reads a list of "refs" as written by either the __match__ or __search__ programs. A ref contains an index number and a filename, along with an optional line number, column number, and string content from the file and line. By giving the ref program a number or a set of numbers, the program will open the given files (with the file specified with the -o option or stored in the EDIT_OPENER environment variable) and attempt to open that file and scroll to the indicated line and column number (if any).

Many text editors and integrated development environments (IDEs) have similar features, so why write these? Sometimes it's useful to use the output of these programs in concert with other unix text processing tools (e.g. sed, awk, grep, wc, sort). Reading and studying and code in a terminal is also useful, especially when looking at new or unfamiliar projects. 

I've had versions of these programs on my computer for years now, and I've gotten a lot of good service from them. Over the year-end break of 2022, I rewrote them for scratch just for fun.

If you want to compile and use them yourself, I regret to say that you're on your own. I didn't take any time or make any substantial effort to make this code usable on anyone's computer by my own. That said, if you have cmake and a c++ compiler handy (I used clang-15 and c++17), you might have some luck with these steps:

1. Get the UU project source code from `https://github.com/kocienda/UU`.
2. Compile and install it with: `cmake -S . -B build; cmake --build build --target install`
3. Get this project's source code from `https://github.com/kocienda/iota`.
4. Compile and install it with: `cmake -S . -B build; cmake --build build --target install`

You many also wish to look at the source code, just for fun, and if you do, I hope you enjoy.

— Ken
