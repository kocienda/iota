//
//  search-tool.cpp
//

#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include <getopt.h>
#include <stdio.h>

#include <UU/UU.h>

extern int optind;

namespace fs = std::filesystem;

static void version(void)
{
    puts("search : version 4.0");
}

static void usage(void)
{
    version();
    puts("");
    puts("Usage: search [options] <search-string>...");
    puts("");
    puts("Options:");
    puts("    -a : Matches any pattern given.");
    puts("    -e : Search pattern is a regular expression.");
    puts("    -h : Prints this help message.");
    puts("    -i : Case insensitive search.");
    puts("    -r : Replace found search patterns with last argument on command line, which must be a string.");
    puts("    -s : Skip files listed in .skippables.");
    puts("    -v : Prints the program version.");
}

static struct option long_options[] =
{
    {"all",              no_argument,       0, 'a'},
    {"help",             no_argument,       0, 'h'},
    {"case-insensitive", no_argument,       0, 'i'},
    {"replace",          no_argument,       0, 'r'},
    {"skip",             no_argument,       0, 's'},
    {"version",          no_argument,       0, 'v'},
    {0, 0, 0, 0}
};

static std::vector<fs::path> build_file_list(const fs::path &dir)
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
        if (UU::is_searchable(UU::searchable_paths(), path)) {
            result.push_back(path);
        }
    }
    return result;
}

const int LineBufferLength = 1024 * 1024;

static char *line_buffer()
{
    static char *line;
    static std::once_flag flag;
    std::call_once(flag, []() { 
        line = static_cast<char *>(malloc(LineBufferLength));
    });
    return line;
}

static void add_line_to_results(const fs::path &path, int line_num, char *line, std::vector<UU::TextRef> &results) 
{
    size_t line_length = strlen(line);
    if (line_length > 1 && line[line_length - 1] == '\n') {
        line[line_length - 1] = '\0';
    }
    int index = results.size() + 1;
    results.push_back(UU::TextRef(index, path, line_num, std::string(line)));
}

enum { CaseSensitiveSearch = 0, CaseInsensitiveSearch = 1 };

std::vector<UU::TextRef> search_file(const fs::path &path, const std::vector<std::string> &search_patterns, int flags)
{
    std::vector<UU::TextRef> results;

    if (search_patterns.size() == 0) {
        return results;
    }

    FILE *file = fopen(path.c_str(), "r");
    if (file == NULL) {
        std::cerr << "error opening file: " << path << ": " << strerror(errno) << std::endl;
        return results;
    }

    char *(*search_fn)(const char *, const char *) = strstr;
    if (flags & CaseInsensitiveSearch) {
        search_fn = strcasestr;
    }
    
    char *line = line_buffer();
    
    int line_num = 0;
    while (fgets(line, LineBufferLength, file)) {
        line_num++;
        bool matches_all = true;
        for (const auto &pattern : search_patterns) {
            if (!matches_all) {
                break;
            }
            if (!search_fn(line, pattern.c_str())) {
                matches_all = false;
            }
        }
        if (matches_all) {
            add_line_to_results(path, line_num, line, results);
        }
    }
    fclose(file);
    
    return results;
}

int main(int argc, char **argv)
{
    bool option_i = false;
    bool option_r = false;
    bool option_s = false;
    unsigned flags = 0;

    while (1) {
        int option_index = 0;
        int c = getopt_long(argc, argv, "hirsv", long_options, &option_index);
        if (c == -1)
            break;
    
        switch (c) {
            case 'h':
                usage();
                return 0;            
            case 'i':
                option_i = true;
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
            case '?':
                version();
                return 0;            
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

    std::string replace;
    if (option_r) {
        replace = argv[argc - 1];        
    }    

    std::vector<std::string> search_patterns;

    int pattern_count = option_r ? argc - 1 : argc;
    for (int i = optind; i < pattern_count; i++) {
        const char *arg = argv[i];
        search_patterns.push_back(arg);
    //     if (arg_is_pattern(arg)) {
    //         char *pat = strdup(arg+1);
    //         pat[strlen(pat)-1] = '\0';
    //         if (strlen(pat)) {
    //             std::regex::flag_type regex_flags = basic | optimize;
    //             if (option_i)
    //                 regex_flags |= icase;
    //             search_patterns.push_back(SearchPattern(SearchPattern::ERegexType, pat, regex(pat, regex_flags)));
    //         }
    //     }
    //     else if (arg_is_file_pattern(arg)) {
    //         char *pat = strdup(arg+1);
    //         pat[strlen(pat)-1] = '\0';
    //         if (strlen(pat))
    //             file_patterns.push_back(pat);
    //     }
    //     else if (option_r && option_i) {
    //         // Strings which are case-insensive are treated like regexes when replacing.
    //         // Easier to implement that way.
    //         std::regex::flag_type regex_flags = basic | optimize | icase;
    //         search_patterns.push_back(SearchPattern(SearchPattern::ERegexType, arg, regex(arg, regex_flags)));
    //     }
    //     else {
            // search_patterns.push_back(arg);
    //     }
    }
    
    fs::path current_path = fs::current_path();

    const auto &file_list = build_file_list(current_path);

    int search_flags = option_i ? CaseInsensitiveSearch : CaseSensitiveSearch;

    std::vector<UU::TextRef> found;
    for (const auto &path : file_list) {
        if (option_r) {
            // vector<TextRef> file_results(fts_search_replace(path, search_patterns, replace, flags));
            // found.insert(found.end(), file_results.begin(), file_results.end());
        }
        else {
            std::vector<UU::TextRef> file_results(search_file(path, search_patterns, search_flags));
            found.insert(found.end(), file_results.begin(), file_results.end());
        }
    }

    int count = 1;
    for (auto &text_ref : found) {
        text_ref.set_index(count);
        count++;
        std::cout << text_ref.to_string(UU::TextRef::AllFeatures, UU::TextRef::FilenameFormat::RELATIVE, current_path) << std::endl;
    }

    const char *refs_path = getenv("REFS_PATH");
    if (refs_path) {
        std::ofstream file(refs_path);
        if (!file.fail()) {
            count = 1;
            for (auto text_ref : found) {
                text_ref.set_index(count);
                count++;
                std::string str = text_ref.to_string(UU::TextRef::AllFeatures, UU::TextRef::FilenameFormat::ABSOLUTE);
                file << str << std::endl;
            }
        } 
    }

    return 0;
}
