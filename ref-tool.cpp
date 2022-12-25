//
// ref-tool.cpp
//

#include <fstream>
#include <iostream>
#include <filesystem>
#include <string>
#include <vector>

#include <getopt.h>

#include <UU/UU.h>

using UU::Span;
using UU::TextRef;
using UU::UInt32;

extern int optind;

static void version(void)
{
    puts("ref : version 4.0");
}

static void usage(void)
{
    version();
    puts("");
    puts("Usage: ref [options] [number]...");
    puts("");
    puts("Options:");
    puts("    -f : Reads refs from given file (default: ENV['REF_PATH']).");
    puts("    -h : Prints this help message.");
    puts("    -o : Opens refs with progam name given (default: ENV['EDIT_OPENER']).");
    puts("    -v : Prints the program version.");
}

static struct option long_options[] =
{
    {"file",    required_argument, 0, 'f'},
    {"help",    no_argument,       0, 'h'},
    {"open",    required_argument, 0, 'o'},
    {"version", no_argument,       0, 'v'},
    {0, 0, 0, 0}
};

int main(int argc, char *argv[])
{
    std::string opener = getenv("EDIT_OPENER");
    std::filesystem::path refs_path = getenv("REFS_PATH");

    while (1) {
        int option_index = 0;
        int c = getopt_long(argc, argv, "f:ho:v", long_options, &option_index);
        if (c == -1)
            break;
    
        switch (c) {
            case 'f':
                refs_path = std::filesystem::path(optarg);
                break;           
            case 'h':
                usage();
                return 0;            
            case 'o':
                opener = optarg;
                break;
            case 'v':
                version();
                return 0;            
            default:
                usage();
                exit(-1);
                break;
        }
    }

    if (refs_path.native().length() == 0) {
        refs_path = std::filesystem::absolute("~/.refs");    
    }

    if (optind >= argc) {
        if (!std::filesystem::exists(refs_path)) {
            std::cerr << "*** ref: unable to open refs file: " << refs_path << std::endl;
            exit(-1);
        }
        execlp("cat", "cat", refs_path.c_str(), NULL);
        exit(0);
    }

    if (opener.empty()) {
        std::cerr << "*** ref: no opener program specified" << std::endl;
        exit(-1);
    }

    std::ifstream file(refs_path);
    if (file.fail()) {
        std::cerr << "*** ref: unable to open refs file: " << refs_path << std::endl;
        return -1;
    } 

    std::string str;
    std::vector<UU::TextRef> refs;
    while (getline(file, str)) {
        UU::TextRef ref(UU::TextRef::from_string(str));    
        if (ref.has_index()) {
            refs.push_back(ref);
        }
    }

    if (refs.size() == 0) {
        return 0;
    }

    Span<UInt32> span;
    for (UInt32 i = optind; i < argc; i++) {
        std::string arg(argv[i]);
        span.add(arg);
    }
    span.simplify();

    if (span.is_empty()) {
        return 0;
    }

    std::vector<std::string> exec_args;

    if (opener == "code") {
        exec_args.push_back("-g");
    }

    for (int sidx : span) {
        if (sidx <= 0 || sidx > refs.size()) {
            continue;
        }
        sidx--;
        const TextRef &ref = refs[sidx];
        exec_args.push_back(ref.to_string(TextRef::Filename | TextRef::Line |  TextRef::Column));
    }

    int rc = UU::launch(opener, exec_args);
    std::cerr << "*** ref: exec error: " << strerror(errno) << std::endl;
    return rc;
}