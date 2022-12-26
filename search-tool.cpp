//
//  search-tool.cpp
//

#include <algorithm>
#include <fstream>
#include <regex>
#include <set>
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
    puts("    -a : Matches any pattern given, rather than requiring a line to match all patterns.");
    puts("    -e : Search pattern is a regular expression.");
    puts("    -h : Prints this help message.");
    puts("    -i : Case insensitive search.");
    puts("    -r : Replace found search patterns with last argument on command line (which is treated as a string).");
    puts("    -s : Search for files in all directories, including those in ENV['SKIPPABLES_PATH'].");
    puts("    -v : Prints the program version.");
}

static struct option long_options[] =
{
    {"all-patterns",      no_argument,  0, 'a'},
    {"regex-search",      no_argument,  0, 'e'},
    {"help",              no_argument,  0, 'h'},
    {"case-insensitive",  no_argument,  0, 'i'},
    {"replace",           no_argument,  0, 'r'},
    {"search-skippables", no_argument,  0, 's'},
    {"version",           no_argument,  0, 'v'},
    {0, 0, 0, 0}
};

enum class PatternType { None, String, Regex };
enum class MatchType { All, Any };
enum class SearchCase { Sensitive, Insensitive };
enum class Skip { SkipNone, SkipSkippables };

static std::vector<fs::path> build_file_list(const fs::path &dir, Skip skip = Skip::SkipSkippables)
{
    std::vector<fs::path> result;
    fs::directory_options options = fs::directory_options::skip_permission_denied;
    for (auto it = fs::recursive_directory_iterator(dir, options); it != fs::recursive_directory_iterator(); ++it) {
        const fs::directory_entry &dir_entry = *it;
        const fs::path &path = dir_entry.path();
        if (dir_entry.is_directory() && skip == Skip::SkipSkippables && UU::is_skippable(UU::skippable_paths(), path)) {
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

std::vector<size_t> find_line_ending_offsets(const std::string_view &str, size_t max_match_start_index)
{
    max_match_start_index = std::min(max_match_start_index, str.length()); 
    std::vector<size_t> result;

    // find all line endings in str up to and including the line with the last match
    for (size_t idx = 0; idx < max_match_start_index; idx++) {
        if (str[idx] == '\n') {
            result.push_back(idx);
        }
    }
    // add the line end after the last match, or if there is none, the last index in the file
    bool added_last_line_ending = false;
    for (size_t idx = max_match_start_index; idx < str.length(); idx++) {
        if (str[idx] == '\n') {
            result.push_back(idx);
            added_last_line_ending = true;
            break;
        }
    }
    if (!added_last_line_ending) {
        result.push_back(str.length()); // one after the end
    }

    return result;
}

class Match
{
public:
    constexpr Match() {}
    Match(PatternType pattern_type, size_t pattern_index, size_t start_index) : 
        m_pattern_type(pattern_type), m_pattern_index(pattern_index), m_start_index(start_index) {}

    PatternType pattern_type() const { return m_pattern_type; }
    void set_pattern_type(PatternType pattern_type) { m_pattern_type = pattern_type; }

    size_t pattern_index() const { return m_pattern_index; }
    void set_pattern_index(size_t pattern_index) { m_pattern_index = pattern_index; }

    size_t start_index() const { return m_start_index; }
    void set_start_index(size_t start_index) { m_start_index = start_index; }

    size_t line_start_index() const { return m_line_start_index; }
    void set_line_start_index(size_t line_start_index) { m_line_start_index = line_start_index; }

    size_t line_length() const { return m_line_length; }
    void set_line_length(size_t line_length) { m_line_length = line_length; }

    size_t line_number() const { return m_line_number; }
    void set_line_number(size_t line_number) { m_line_number = line_number; }

    size_t column_number() const { return m_column_number; }
    void set_column_number(size_t column_number) { m_column_number = column_number; }

private:
    PatternType m_pattern_type = PatternType::None;
    size_t m_pattern_index = 0;
    size_t m_start_index = 0;
    size_t m_line_start_index = 0;
    size_t m_line_length = 0;
    size_t m_line_number = 0;
    size_t m_column_number = 0;
};

static void add_line_to_results(const fs::path &path, size_t line_num, size_t column_num, const std::string &line, std::vector<TextRef> &results) 
{
    size_t index = results.size() + 1;
    results.push_back(TextRef(index, path, line_num, column_num, line));
}

std::vector<TextRef> search_file(const fs::path &path, const std::vector<std::string> &string_patterns, SearchCase search_case,
    const std::vector<std::regex> &regex_patterns, MatchType match_type)
{
    std::vector<TextRef> results;

    UU::MappedFile mapped_file(path);
    if (mapped_file.is_valid<false>()) {
        return results;
    }

    std::string_view source((char *)mapped_file.base(), mapped_file.file_length());
    std::string_view haystack((char *)mapped_file.base(), mapped_file.file_length());
    std::string case_folded_string;

    if (search_case == SearchCase::Insensitive) {
        case_folded_string = std::string(haystack);
        std::transform(case_folded_string.cbegin(), case_folded_string.cend(), case_folded_string.begin(), 
            [](unsigned char c) { return std::tolower(c); });
        haystack = case_folded_string;
    }

    std::vector<Match> matches;

    size_t pattern_index = 0;
    for (const auto &string_pattern : string_patterns) {
        const auto searcher = std::boyer_moore_searcher(string_pattern.begin(), string_pattern.end());
        auto hit = haystack.begin();
        while (true) {
            auto it = std::search(hit, haystack.end(), searcher);
            if (it == haystack.end()) {
                break;
            }
            matches.emplace_back(PatternType::String, pattern_index, it - haystack.begin());
            hit = ++it;
        }
        pattern_index++;
    }

    for (const auto &regex_pattern : regex_patterns) {
        const auto searcher_begin = std::cregex_iterator(haystack.begin(), haystack.end(), regex_pattern);
        auto searcher_end = std::cregex_iterator();
        for (auto it = searcher_begin; it != searcher_end; ++it) {
            const auto &match = *it;                                                 
            matches.emplace_back(PatternType::Regex, pattern_index, match.position());
        }   
        pattern_index++;
    }

    if (matches.size() == 0) {
        return results;
    }

    std::sort(matches.begin(), matches.end(), [](const Match &a, const Match &b) { 
        return a.start_index() < b.start_index(); 
    });

    // set all the metadata for the match, mostly by finding the line for the match in haystack
    std::vector<size_t> haystack_line_end_offsets = find_line_ending_offsets(haystack, matches.back().start_index());
    size_t line_number = 0;
    for (auto &match : matches) {
        while (haystack_line_end_offsets[line_number] < match.start_index()) {
            line_number++;
            ASSERT(line_number < haystack_line_end_offsets.size());
        }
        size_t sidx = line_number == 0 ? 0 : (haystack_line_end_offsets[line_number -1] + 1);
        size_t eidx = haystack_line_end_offsets[line_number];
        match.set_line_start_index(sidx);
        match.set_line_length(eidx - sidx);
        match.set_line_number(line_number + 1);
        match.set_column_number(match.start_index() - sidx + 1);
    }

    // if MatchType is All and there's more than one pattern, 
    // filter each line's worth of matches to ensure each pattern matches
    size_t pattern_count = string_patterns.size() + regex_patterns.size();
    if (match_type == MatchType::All && pattern_count > 1) {
        std::vector<Match> filtered_matches;
        size_t current_line = 0;
        std::set<size_t> matched_pattern_indexes;
        size_t sidx = 0;
        size_t idx = 0;
        for (const auto &match : matches) {
            if (current_line == 0) {
                current_line = match.line_number();    
            }
            if (current_line == match.line_number()) {
                matched_pattern_indexes.insert(match.pattern_index());
            }
            else {
                if (matched_pattern_indexes.size() == pattern_count) {
                    filtered_matches.insert(filtered_matches.end(), matches.begin() + sidx, matches.begin() + idx);
                }
                current_line = match.line_number();
                matched_pattern_indexes.clear();
                matched_pattern_indexes.insert(match.pattern_index());
                sidx = idx;
            }
            idx++;
        }
        if (matched_pattern_indexes.size() == pattern_count) {
            filtered_matches.insert(filtered_matches.end(), matches.begin() + sidx, matches.begin() + idx);
        }
        matches = filtered_matches;
    }

    for (auto &match : matches) {
        // extract the string and add the result
        std::string line = std::string(haystack.substr(match.line_start_index(), match.line_length()));
        add_line_to_results(path, match.line_number(), match.column_number(), line, results);
    }

    return results;
}

static void report_results(const fs::path &current_path, std::vector<TextRef> &results) {
    std::sort(results.begin(), results.end(), std::less<TextRef>());
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
    bool option_a = false;
    bool option_e = false;
    bool option_i = false;
    bool option_r = false;
    bool option_s = false;

    while (1) {
        int option_index = 0;
        int c = getopt_long(argc, argv, "aehirsv", long_options, &option_index);
        if (c == -1)
            break;
    
        switch (c) {
            case 'a':
                option_a = true;
                break;
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

    std::regex::flag_type regex_flags = std::regex::egrep | std::regex::optimize;
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
            std::string pattern(arg);
            if (option_i) {
                std::transform(pattern.cbegin(), pattern.cend(), pattern.begin(), [](unsigned char c) { return std::tolower(c); });    
            }
            string_patterns.push_back(pattern);
        }
    }
    
    SearchCase search_case = option_i ? SearchCase::Insensitive : SearchCase::Sensitive;
    MatchType match_type = option_a ? MatchType::Any : MatchType::All;

    fs::path current_path = fs::current_path();
    const auto &file_list = build_file_list(current_path, option_s ? Skip::SkipNone : Skip::SkipSkippables);

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
                std::vector<TextRef> file_results = search_file(path, string_patterns, search_case, regex_patterns, match_type);
                dispatch_async(completion_queue, ^{
                    completion_block(file_results);    
                });
            });
        }
    }

    dispatch_main();

    return 0;
}
