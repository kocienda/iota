//
//  match-tool.cpp
//

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <fnmatch.h>
#include <getopt.h>

#include <UU/UU.h>

extern int optind;

namespace fs = std::filesystem;

static void version(void)
{
    puts("match : version 4.0");
}

static void usage(void)
{
    version();
    puts("");
    puts("Usage: match [options] [pattern]...");
    puts("");
    puts("Options:");
    puts("    -c : Matches patterns with case sensitivity.");
    puts("    -e : Matches must be exact.");
    puts("    -f : Prints full paths of matched files to stdout.");
    puts("    -h : Prints this help message.");
    puts("    -o : Opens matched files with progam name given.");
    puts("    -p : Write filenames to stdout without numbers; good for piping results to other programs");
    puts("    -r : Writes numbered file references to ENV['REFS_PATH'].");
    puts("    -s : Matches all patterns separately, instead of searching within previous results.");
    puts("    -v : Prints the program version.");
    puts("    -1 : Stop at first match found.");
}

static struct option long_options[] =
{
    {"case",      no_argument,       0, 'c'},
    {"exact",     no_argument,       0, 'e'},
    {"full path", no_argument,       0, 'f'},
    {"help",      no_argument,       0, 'h'},
    {"open",      optional_argument, 0, 'o'},
    {"pipe",      optional_argument, 0, 'p'},
    {"refs",      no_argument,       0, 'r'},
    {"separate",  no_argument,       0, 's'},
    {"version",   no_argument,       0, 'v'},
    {"one match", no_argument,       0, '1'},
    {0, 0, 0, 0}
};

static std::vector<fs::path> find_matches(const fs::path &dir, const std::vector<std::string> &patterns, int flags)
{
    std::vector<fs::path> result;
    fs::directory_options options = fs::directory_options::skip_permission_denied;
    for (auto it = fs::recursive_directory_iterator(dir, options); it != fs::recursive_directory_iterator(); ++it) {
        const fs::directory_entry &dir_entry = *it;
        const fs::path &path = dir_entry.path();
        if (dir_entry.is_directory() && UU::is_skippable(UU::skippable_paths(), path)) {
            it.disable_recursion_pending();
            continue;
        }
        if (!dir_entry.is_regular_file()) {
            continue;
        }
        for (const auto &pattern : patterns) {
            if (UU::filename_match(pattern, path, flags)) {
                result.push_back(path);
                break;
            }
        }
    }
    return result;
}

int main(int argc, char *argv[])
{
    std::string opener;
    bool option_c = false;
    bool option_e = false;
    bool option_f = false;
    bool option_p = false;
    bool option_r = false;
    bool option_s = false;
    bool option_1 = false;

    while (1) {
        int option_index = 0;
        int c = getopt_long(argc, argv, "cefho:prsv1", long_options, &option_index);
        if (c == -1)
            break;
        switch (c) {
            case 'c':
                option_c = true;
                break;
            case 'e':
                option_e = true;
                break;
            case 'f':
                option_f = true;
                break;
            case 'h':
                usage();
                return 0;            
            case 'o':
                opener = optarg;
                break;
            case 'p':
                option_p = true;
                break;
            case 'r':
                option_r = true;
                break;
            case 's':
                option_s = true;
                break;
            case 'v':
                version();
                return 0;            
            case '1':
                option_1 = true;
                break;            
            default:
                usage();
                exit(-1);
                break;
        }
    }

    if (optind >= argc) {
        usage();
        exit(-1);
    }

    std::vector<fs::path> matches;
    std::vector<std::string> patterns;

    fs::path cwd(fs::current_path());
    fs::path dir;
    fs::path prevdir;

    int filename_match_flags = 0;
    if (option_c) {
        filename_match_flags |= UU::FilenameMatchCaseSensitive;  
    }
    if (option_e) {
        filename_match_flags |= UU::FilenameMatchExact;  
    }

    int loop_end = option_s ? argc : optind + 1;

    for (int i = optind; i < loop_end; i++) {
        std::string str(argv[i]);
        if (str.length() == 0) {
            continue;
        }

        fs::path path = str;

        // determine directory
        if (path.is_absolute()) {
            dir = path;
        }
        else if (str.find_first_of('/') != std::string::npos) {
            dir = fs::relative(cwd, path);
        }
        else {
            dir = cwd;
        }
        patterns.push_back(str);

        // add to pattern list
        if (!prevdir.empty()) {
            prevdir = dir;
        }
        else if (dir != prevdir) {
            std::vector<fs::path> submatches(find_matches(dir, patterns, filename_match_flags));
            matches.insert(matches.end(), submatches.begin(), submatches.end());
            prevdir = dir;
            patterns.clear();
        }
    }

    if (patterns.size()) {
        std::vector<fs::path> submatches(find_matches(dir, patterns, filename_match_flags));
        matches.insert(matches.end(), submatches.begin(), submatches.end());
    }

    // search within previously found matches if needed
    if (!option_s) {
        for (int i = loop_end; i < argc; i++) {
            if (matches.size() == 0) {
                break;
            }
            std::string pattern = argv[i];
            std::vector<fs::path> filtered_matches;
            for (const auto &match : matches) {
                if (UU::filename_match(pattern, match, filename_match_flags)) {
                    filtered_matches.push_back(match);
                }
            }
            matches = filtered_matches;
        }
    }

    // generate output
    std::stringstream file_out;
    int count = 0;
    for (const auto &match : matches) {
        count++;
        if (option_p) {
            if (option_f) {
                std::cout << UU::shell_escaped_string(match.c_str()) << " ";
            }
            else {
                std::cout << UU::shell_escaped_string(match.filename().c_str()) << " ";
            }
        }
        else {
            const std::string &full_path = UU::shell_escaped_string(match.c_str());
            file_out << count << ") " << full_path << "\n";
            if (option_f) {
                std::cout << count << ") " << full_path << "\n";
            }
            else {
                std::cout << count << ") " << UU::shell_escaped_string(match.filename().c_str()) << "\n";
            }
        }
    }

    // write to file if needed
    if (option_r) {
        const char *refs_path = getenv("REFS_PATH");
        if (refs_path) {
            std::ofstream file(refs_path);
            if (!file.fail()) {
                file << file_out.str();
            } 
        }
    }

    // open matches if needed
    if (!opener.empty() && matches.size()) {
        std::vector<std::string> exec_args;
        if (opener == "code") {
            exec_args.push_back("-g");
        }
        for (const auto &match : matches) { 
            exec_args.push_back(match.c_str());
        }
        int rc = UU::launch(opener, exec_args);
        std::cerr << "*** match: exec error: " << strerror(errno) << std::endl;
        return rc;
    }

    return 0;
}