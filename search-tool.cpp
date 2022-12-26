//
//  search-tool.cpp
//

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
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

using UU::TextRef;

enum class MatchType { All, Any };
enum class SearchCase { Sensitive, Insensitive };
enum class Skip { SkipNone, SkipSkippables };
enum class Mode { Search, SearchAndReplace };
enum class HighlightColor {
    None = 0,
    Black = 30,
    Red = 31,
    Green = 32,
    Yellow = 33,
    Blue = 34,
    Magenta = 35,
    Cyan = 36,
    White = 37,
    BrightBlack = 90,
    BrightRed = 91,
    BrightGreen = 92,
    BrightYellow = 93,
    BrightBlue = 94,
    BrightMagenta = 95,
    BrightCyan = 96,
    BrightWhite = 97,
};

static HighlightColor highlight_color_from_string(const std::string &s)
{
    std::map<std::string, HighlightColor> highlight_colors = {
        {"black", HighlightColor::Black},
        {"red", HighlightColor::Red},
        {"green", HighlightColor::Green},
        {"yellow", HighlightColor::Yellow},
        {"blue", HighlightColor::Blue},
        {"magenta", HighlightColor::Magenta},
        {"white", HighlightColor::White},
        {"brightblack", HighlightColor::BrightBlack},
        {"brightred", HighlightColor::BrightRed},
        {"brightgreen", HighlightColor::BrightGreen},
        {"brightyellow", HighlightColor::BrightYellow},
        {"brightblue", HighlightColor::BrightBlue},
        {"brightmagenta", HighlightColor::BrightMagenta},
        {"brightcyan", HighlightColor::BrightCyan},
        {"brightwhite", HighlightColor::BrightWhite},
    };

    const auto r = highlight_colors.find(s);
    return r == highlight_colors.end() ? HighlightColor::None : r->second;
}

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
    Match() {}
    Match(size_t needle_index, size_t match_start_index, size_t match_extent) : 
        m_needle_index(needle_index), m_match_start_index(match_start_index), m_match_extent(match_extent) {}

    size_t needle_index() const { return m_needle_index; }
    void set_needle_index(size_t needle_index) { m_needle_index = needle_index; }

    size_t match_start_index() const { return m_match_start_index; }
    void set_match_start_index(size_t match_start_index) { m_match_start_index = match_start_index; }

    size_t match_extent() const { return m_match_extent; }
    void set_match_extent(size_t match_extent) { m_match_extent = match_extent; }

    size_t line_start_index() const { return m_line_start_index; }
    void set_line_start_index(size_t line_start_index) { m_line_start_index = line_start_index; }

    size_t line_length() const { return m_line_length; }
    void set_line_length(size_t line_length) { m_line_length = line_length; }

    size_t line_number() const { return m_line_number; }
    void set_line_number(size_t line_number) { m_line_number = line_number; }

    size_t column_number() const { return m_column_number; }
    void set_column_number(size_t column_number) { m_column_number = column_number; }

private:
    size_t m_needle_index = 0;
    size_t m_match_start_index = 0;
    size_t m_match_extent = 0;
    size_t m_line_start_index = 0;
    size_t m_line_length = 0;
    size_t m_line_number = 0;
    size_t m_column_number = 0;
};

std::vector<TextRef> process_file(const fs::path &path, Mode mode, MatchType match_type, 
    const std::vector<std::string> &string_needles, SearchCase search_case, const std::vector<std::regex> &regex_needles, 
    const std::string &replacement)
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

    size_t needle_index = 0;
    for (const auto &string_needle : string_needles) {
        const auto searcher = std::boyer_moore_searcher(string_needle.begin(), string_needle.end());
        auto hit = haystack.begin();
        while (true) {
            auto it = std::search(hit, haystack.end(), searcher);
            if (it == haystack.end()) {
                break;
            }
            matches.emplace_back(needle_index, it - haystack.begin(), string_needle.length());
            hit = ++it;
        }
        needle_index++;
    }

    for (const auto &regex_needle : regex_needles) {
        const auto searcher_begin = std::cregex_iterator(haystack.begin(), haystack.end(), regex_needle);
        auto searcher_end = std::cregex_iterator();
        for (auto it = searcher_begin; it != searcher_end; ++it) {
            const auto &match = *it;                                                 
            matches.emplace_back(needle_index, match.position(), match.length());
        }   
        needle_index++;
    }

    if (matches.size() == 0) {
        return results;
    }

    // code below needs needles sorted by start index,
    // but only do the work if there is more than one needle
    size_t needle_count = string_needles.size() + regex_needles.size();
    if (needle_count > 1) {
        std::sort(matches.begin(), matches.end(), [](const Match &a, const Match &b) { 
            return a.match_start_index() < b.match_start_index(); 
        });
    }

    // set all the metadata for the match, mostly by finding the line for the match in haystack
    std::vector<size_t> haystack_line_end_offsets = find_line_ending_offsets(haystack, matches.back().match_start_index());
    size_t line_number = 0;
    for (auto &match : matches) {
        while (haystack_line_end_offsets[line_number] < match.match_start_index()) {
            line_number++;
            ASSERT(line_number < haystack_line_end_offsets.size());
        }
        size_t sidx = line_number == 0 ? 0 : (haystack_line_end_offsets[line_number -1] + 1);
        size_t eidx = haystack_line_end_offsets[line_number];
        match.set_line_start_index(sidx);
        match.set_line_length(eidx - sidx);
        match.set_line_number(line_number + 1);
        match.set_column_number(match.match_start_index() - sidx + 1);
    }

    // if MatchType is All and there's more than one needle, 
    // filter each line's worth of matches to ensure each needle matches
    if (match_type == MatchType::All && needle_count > 1) {
        std::vector<Match> filtered_matches;
        size_t current_line = 0;
        std::set<size_t> matched_needle_indexes;
        size_t sidx = 0;
        size_t idx = 0;
        for (const auto &match : matches) {
            if (current_line == 0) {
                current_line = match.line_number();    
            }
            if (current_line == match.line_number()) {
                matched_needle_indexes.insert(match.needle_index());
            }
            else {
                if (matched_needle_indexes.size() == needle_count) {
                    filtered_matches.insert(filtered_matches.end(), matches.begin() + sidx, matches.begin() + idx);
                }
                current_line = match.line_number();
                matched_needle_indexes.clear();
                matched_needle_indexes.insert(match.needle_index());
                sidx = idx;
            }
            idx++;
        }
        if (matched_needle_indexes.size() == needle_count) {
            filtered_matches.insert(filtered_matches.end(), matches.begin() + sidx, matches.begin() + idx);
        }
        matches = filtered_matches;
    }

    if (mode == Mode::Search) {
        // add a TextRef for each match
        for (auto &match : matches) {
            size_t index = results.size() + 1;
            std::string line = std::string(source.substr(match.line_start_index(), match.line_length()));
            results.push_back(TextRef(index, path, match.line_number(), match.column_number(), match.match_extent(), line));
        }
        return results;
    }

    ASSERT(mode == Mode::SearchAndReplace);

    return results;
}

static void report_results(const fs::path &current_path, std::vector<TextRef> &results, HighlightColor highlight_color) {
    std::sort(results.begin(), results.end(), std::less<TextRef>());
    int count = 1;
    for (auto &ref : results) {
        ref.set_index(count);
        count++;
        std::cout << ref.to_string(TextRef::AllFeatures, 
            TextRef::FilenameFormat::RELATIVE, current_path, static_cast<int>(highlight_color)) << std::endl;
    }

    const char *refs_path = getenv("REFS_PATH");
    if (refs_path) {
        std::ofstream file(refs_path);
        if (!file.fail()) {
            count = 1;
            for (auto ref : results) {
                ref.set_index(count);
                count++;
                std::string str = ref.to_string(TextRef::StandardFeatures, TextRef::FilenameFormat::ABSOLUTE);
                file << str << std::endl;
            }
        } 
    }

    exit(0);
}

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
    puts("    -a : Matches any needle given, rather than requiring a line to match all needles.");
    puts("    -c : Print results with the given highlight color. Implies printing to a terminal.");
    puts("         ");
    puts("    -e : Search needles are compiles as regular expressions.");
    puts("    -h : Prints this help message.");
    puts("    -i : Case insensitive search.");
    puts("    -r : Search and replace. Takes two arguments: <search> <replacement>");
    puts("                             <search> can be a string or a regex (when invoked with -e)");
    puts("                             <replacement> is always treated as a string");
    puts("    -s : Search for files in all directories, including those in ENV['SKIPPABLES_PATH'].");
    puts("    -v : Prints the program version.");
}

static struct option long_options[] =
{
    {"all-needles",       no_argument,       0, 'a'},
    {"highlight-color",   required_argument, 0, 'c'},
    {"regex-search",      no_argument,       0, 'e'},
    {"help",              no_argument,       0, 'h'},
    {"case-insensitive",  no_argument,       0, 'i'},
    {"replace",           no_argument,       0, 'r'},
    {"search-skippables", no_argument,       0, 's'},
    {"version",           no_argument,       0, 'v'},
    {0, 0, 0, 0}
};

int main(int argc, char **argv)
{
    bool option_a = false;
    bool option_e = false;
    bool option_i = false;
    bool option_r = false;
    bool option_s = false;

    std::string option_c = "";

    while (1) {
        int option_index = 0;
        int c = getopt_long(argc, argv, "ac:ehirsv", long_options, &option_index);
        if (c == -1)
            break;
    
        switch (c) {
            case 'a':
                option_a = true;
                break;
            case 'c':
                option_c = std::string(optarg);
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
    
    std::vector<std::string> string_needles;

    std::vector<std::regex> regex_needles;
    std::regex::flag_type regex_flags = std::regex::egrep | std::regex::optimize;
    if (option_i) {
        regex_flags |= std::regex::icase;
    }

    int needle_count = option_r ? argc - 1 : argc;

    std::string replacement;
    if (option_r) {
        if (needle_count - optind != 1) {
            usage();
            puts("");
            puts("*** search and replace takes exactly two arguments");
            exit(-1);
        }
        replacement = argv[argc - 1];        
    }    

    for (int i = optind; i < needle_count; i++) {
        const char *arg = argv[i];
        if (option_e) {
            regex_needles.push_back(std::regex(arg, regex_flags));
        }
        else {
            std::string needle(arg);
            if (option_i) {
                std::transform(needle.cbegin(), needle.cend(), needle.begin(), [](unsigned char c) { return std::tolower(c); });    
            }
            string_needles.push_back(needle);
        }
    }
    
    SearchCase search_case = option_i ? SearchCase::Insensitive : SearchCase::Sensitive;
    MatchType match_type = option_a ? MatchType::Any : MatchType::All;
    Mode mode = option_r ? Mode::SearchAndReplace : Mode::Search;
    HighlightColor highlight_color = HighlightColor::None;
    if (option_c.length() > 0) {
        highlight_color = highlight_color_from_string(option_c);
        if (highlight_color == HighlightColor::None) {
            usage();
            puts("");
            std::cout << "*** unsupported highlight color: " << option_c << std::endl;
            exit(-1);
        }
    }

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
            report_results(current_path, found, highlight_color);
        }
    };

    for (const auto &path : file_list) {
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^{
            std::vector<TextRef> file_results = process_file(path, mode, match_type, string_needles, search_case, regex_needles, replacement);
            dispatch_async(completion_queue, ^{
                completion_block(file_results);    
            });
        });
    }

    dispatch_main();

    return 0;
}
