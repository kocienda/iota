//
//  search-tool.cpp
//

#include <algorithm>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#include <dispatch/dispatch.h>
#include <getopt.h>
#include <stdio.h>

#include <UU/UU.h>

extern int optind;

namespace fs = std::filesystem;

using UU::Any;
using UU::TextRef;

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
    {"all",               no_argument,       0, 'a'},
    {"search with regex", no_argument,       0, 'e'},
    {"help",              no_argument,       0, 'h'},
    {"case-insensitive",  no_argument,       0, 'i'},
    {"replace",           no_argument,       0, 'r'},
    {"skip",              no_argument,       0, 's'},
    {"version",           no_argument,       0, 'v'},
    {0, 0, 0, 0}
};

enum class SearchCase { Sensitive, Insensitive };

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

static void add_line_to_results(const fs::path &path, size_t line_num, const std::string &line, std::vector<TextRef> &results) 
{
    size_t index = results.size() + 1;
    results.push_back(TextRef(index, path, line_num, line));
}

std::vector<TextRef> search_file_s(const fs::path &path, const std::vector<std::string> &needles)
{
    std::vector<TextRef> results;

    std::string haystack = UU::get_file_contents_as_string(path);

    std::vector<size_t> found_offsets;
    for (const auto &needle : needles) {
        const std::boyer_moore_searcher searcher(needle.begin(), needle.end());
        auto hit = haystack.begin();
        while (true) {
            auto it = std::search(hit, haystack.end(), searcher);
            if (it == haystack.end()) {
                break;
            }
            found_offsets.push_back(it - haystack.begin());
            hit = ++it;
        }
    }
    if (found_offsets.size() > 0) {
        // maintain line count
        std::vector<size_t> line_offsets;
        line_offsets.push_back(0);

        for (const auto offset : found_offsets) {
            // search backward for start of line
            size_t sidx = offset;
            while (sidx > 0) {
                if (haystack[sidx - 1] == '\n') {
                    break;
                }
                sidx--;
            }
            // search forward for end of line
            size_t eidx = offset;
            while (eidx < haystack.length()) {
                if (haystack[eidx] == '\n') {
                    break;
                }
                eidx++;
            }
            std::string line = haystack.substr(sidx, eidx - sidx);

            // count lines
            for (auto idx = line_offsets.back() + 1; idx < eidx; idx++) {
                if (haystack[idx] == '\n') {
                    line_offsets.push_back(idx);
                }
            }

            add_line_to_results(path, line_offsets.size(), line, results);
        }
    }

    return results;
}

static void report_results(const fs::path &current_path, std::vector<TextRef> &results) {
    int count = 1;
    for (auto &ref : results) {
        ref.set_index(count);
        count++;
        std::cout << ref.to_string(TextRef::AllFeatures, TextRef::FilenameFormat::RELATIVE, current_path) << std::endl;
    }

    const char *refs_path = getenv("REFS_PATH");
    if (refs_path) {
        std::ofstream file(refs_path);
        if (!file.fail()) {
            count = 1;
            for (auto ref : results) {
                ref.set_index(count);
                count++;
                std::string str = ref.to_string(TextRef::AllFeatures, TextRef::FilenameFormat::ABSOLUTE);
                file << str << std::endl;
            }
        } 
    }

    exit(0);
}

int main(int argc, char **argv)
{
    bool option_e = false;
    bool option_i = false;
    bool option_r = false;
    bool option_s = false;

    while (1) {
        int option_index = 0;
        int c = getopt_long(argc, argv, "ehirsv", long_options, &option_index);
        if (c == -1)
            break;
    
        switch (c) {
            case 'e':
                option_e = true;
                break;
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

    std::vector<std::string> string_patterns;
    std::vector<std::regex> regex_patterns;

    std::regex::flag_type regex_flags = std::regex::grep | std::regex::optimize;
    if (option_i) {
        regex_flags |= std::regex::icase;
    }

    int pattern_count = option_r ? argc - 1 : argc;
    for (int i = optind; i < pattern_count; i++) {
        const char *arg = argv[i];
        if (option_e) {
            regex_patterns.push_back(std::regex(arg, regex_flags));
        }
        else {
            string_patterns.push_back(arg);
        }
    //     else if (option_r && option_i) {
    //         // Strings which are case-insensive are treated like regexes when replacing.
    //         // Easier to implement that way.
    //         std::regex::flag_type regex_flags = basic | optimize | icase;
    //         string_patterns.push_back(SearchPattern(SearchPattern::ERegexType, arg, regex(arg, regex_flags)));
    //     }
    //     else {
            // string_patterns.push_back(arg);
    //     }
    }
    
    std::cout << "string_patterns.size: " << string_patterns.size() << std::endl;
    std::cout << "regex_patterns.size:  " << regex_patterns.size() << std::endl;

    fs::path current_path = fs::current_path();
    const auto &file_list = build_file_list(current_path);
    // SearchCase search_case = option_i ? SearchCase::Insensitive : SearchCase::Sensitive;

    __block std::vector<TextRef> found;
    __block int completions = 0;
    const size_t expected_completions = file_list.size();
    dispatch_queue_t completion_queue = dispatch_queue_create("search-tool", DISPATCH_QUEUE_SERIAL);
    void (^completion_block)(const std::vector<TextRef> &) = ^void(const std::vector<TextRef> &file_results) {
        found.insert(found.end(), file_results.begin(), file_results.end());
        completions++;
        if (completions == expected_completions) {
            report_results(current_path, found);
        }
    };

    for (const auto &path : file_list) {
        if (option_r) {
            // vector<TextRef> file_results(fts_search_replace(path, string_patterns, replace, flags));
            // found.insert(found.end(), file_results.begin(), file_results.end());
        }
        else {
            dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^{
                std::vector<TextRef> file_results;
                if (option_e) {

                }
                else if (option_i) {
                }
                else {
                    file_results = search_file_s(path, string_patterns);
                }
                // std::vector<TextRef> file_results(search_file(path, string_patterns, regex_patterns, search_flags));
                dispatch_async(completion_queue, ^{
                    completion_block(file_results);    
                });
            });
        }
    }

    dispatch_main();

    return 0;
}
