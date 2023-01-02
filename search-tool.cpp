//
// search-tool.cpp
//
// MIT License
// Copyright (c) 2022 Ken Kocienda. All rights reserved.
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include <getopt.h>
#include <stdio.h>

#include <UU/UU.h>

#define USE_FUTURES 0

#define USE_DISPATCH (PLATFORM(MAC) && !USE_FUTURES)

#if USE_DISPATCH
#include <dispatch/dispatch.h>
#else
#include <future>
#include <semaphore>
// this semaphore limits the number of concurrent searches
const int good_concurrency_count = UU::get_good_concurrency_count() - 1;
std::counting_semaphore g_semaphore(good_concurrency_count);
#endif

extern int optind;

namespace fs = std::filesystem;

using UU::MappedFile;
using UU::Span;
using UU::SizeType;
using UU::TextRef;

enum class Skip { SkipNone, SkipSkippables };
enum class Mode { Search, SearchAndReplace, SearchAndReplaceDryRun };
enum class MatchType { All, Any };
enum class SearchCase { Sensitive, Insensitive };
enum class HighlightColor {
    None = 0,
    Black = 30,
    Gray = 90,
    Red = 91,
    Green = 92,
    Yellow = 93,
    Blue = 94,
    Magenta = 95,
    Cyan = 96,
    White = 97,
};
enum class MergeSpans { No, Yes };

class Env
{
public:
    Env(const fs::path &current_path,
        const std::vector<std::string> &string_needles,
        const std::vector<std::regex> &regex_needles,
        const std::string &replacement,
        HighlightColor highlight_color,
        MatchType match_type,
        MergeSpans merge_spans,
        Mode mode,
        SearchCase search_case) :
        m_current_path(current_path),
        m_string_needles(string_needles),
        m_regex_needles(regex_needles),
        m_replacement(replacement),
        m_highlight_color(highlight_color),
        m_match_type(match_type),
        m_merge_spans(merge_spans),
        m_mode(mode),
        m_search_case(search_case) 
    {}

    const fs::path &current_path() const { return m_current_path; }
    const std::vector<std::string> &string_needles() const { return m_string_needles; }
    const std::vector<std::regex> &regex_needles() const { return m_regex_needles; }
    const std::string &replacement() const { return m_replacement; }
    HighlightColor highlight_color() const { return m_highlight_color; }
    MatchType match_type() const { return m_match_type; }
    MergeSpans merge_spans() const { return m_merge_spans; }
    Mode mode() const { return m_mode; }
    SearchCase search_case() const { return m_search_case; }

private:
    fs::path m_current_path;
    std::vector<std::string> m_string_needles;
    std::vector<std::regex> m_regex_needles;
    std::string m_replacement;
    std::vector<TextRef> m_text_refs;
    HighlightColor m_highlight_color;
    MatchType m_match_type;
    MergeSpans m_merge_spans;
    Mode m_mode;
    SearchCase m_search_case;
};

static HighlightColor highlight_color_from_string(const std::string &s)
{
    std::map<std::string, HighlightColor> highlight_colors = {
        {"black", HighlightColor::Black},
        {"gray", HighlightColor::Gray},
        {"red", HighlightColor::Red},
        {"green", HighlightColor::Green},
        {"yellow", HighlightColor::Yellow},
        {"blue", HighlightColor::Blue},
        {"magenta", HighlightColor::Magenta},
        {"cyan", HighlightColor::Cyan},
        {"white", HighlightColor::White},
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

class Match
{
public:
    Match() {}
    Match(SizeType needle_index, SizeType match_start_index, SizeType match_length) : 
        m_needle_index(needle_index), m_span(match_start_index, match_start_index + match_length) {}

    SizeType needle_index() const { return m_needle_index; }
    void set_needle_index(SizeType needle_index) { m_needle_index = needle_index; }

    SizeType match_start_index() const { return m_span.first(); }

    const Span<SizeType> &span() const { return m_span; }
    void add_span(const Span<SizeType> &span) { m_span.add(span); }
    void simplify_span() { m_span.simplify(); }

    SizeType line_start_index() const { return m_line_start_index; }
    void set_line_start_index(SizeType line_start_index) { m_line_start_index = line_start_index; }

    SizeType line_length() const { return m_line_length; }
    void set_line_length(SizeType line_length) { m_line_length = line_length; }

    SizeType line() const { return m_line; }
    void set_line(SizeType line) { m_line = line; }

    SizeType column() const { return match_start_index() - m_line_start_index; }

private:
    SizeType m_needle_index = 0;
    Span<SizeType> m_span;
    SizeType m_line_start_index = 0;
    SizeType m_line_length = 0;
    SizeType m_line = 0;
};

std::vector<TextRef> process_file(const fs::path &filename, const Env &env)
{
#if !USE_DISPATCH
    // The guard releases the semaphore regardless of how the function exits
    UU::AcquireReleaseGuard semaphore_guard(g_semaphore);
#endif

    std::vector<TextRef> results;

    MappedFile mapped_file(filename);
    if (mapped_file.is_valid<false>()) {
        return results;
    }

    std::string_view source((char *)mapped_file.base(), mapped_file.file_length());
    std::string_view haystack((char *)mapped_file.base(), mapped_file.file_length());
    
    std::string case_folded_string;
    if (env.search_case() == SearchCase::Insensitive) {
        case_folded_string = std::string(haystack);
        std::transform(case_folded_string.cbegin(), case_folded_string.cend(), case_folded_string.begin(), 
            [](unsigned char c) { return std::tolower(c); });
        haystack = case_folded_string;
    }

    std::vector<Match> matches;
    SizeType needle_index = 0;

    // do string searches
    for (const auto &string_needle : env.string_needles()) {
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

    // do regex searches
    for (const auto &regex_needle : env.regex_needles()) {
        const auto searcher_begin = std::cregex_iterator(haystack.begin(), haystack.end(), regex_needle);
        auto searcher_end = std::cregex_iterator();
        for (auto it = searcher_begin; it != searcher_end; ++it) {
            const auto &match = *it;                                                 
            matches.emplace_back(needle_index, match.position(), match.length());
        }   
        needle_index++;
    }

    // return if nothing found
    if (matches.size() == 0) {
        return results;
    }

    // code below needs needles sorted by start index,
    // but only do the work if there is more than one needle
    SizeType needle_count = env.string_needles().size() + env.regex_needles().size();
    if (needle_count > 1) {
        std::sort(matches.begin(), matches.end(), [](const Match &a, const Match &b) { 
            return a.match_start_index() < b.match_start_index(); 
        });
    }

    // set line-related metadata for the match
    std::vector<SizeType> haystack_line_end_offsets = UU::find_line_end_offsets(haystack, matches.back().match_start_index());
    SizeType line = 0;
    for (auto &match : matches) {
        while (haystack_line_end_offsets[line] < match.match_start_index()) {
            line++;
            ASSERT(line < haystack_line_end_offsets.size());
        }
        SizeType sidx = line == 0 ? 0 : (haystack_line_end_offsets[line -1] + 1);
        SizeType eidx = haystack_line_end_offsets[line];
        match.set_line_start_index(sidx);
        match.set_line_length(eidx - sidx);
        match.set_line(line + 1);
    }

    // if MatchType is All and there's more than one needle, 
    // filter each line's worth of matches to ensure each needle matches
    if (env.match_type() == MatchType::All && needle_count > 1) {
        std::vector<Match> filtered_matches;
        filtered_matches.reserve(matches.size());
        SizeType current_line = 0;
        std::set<SizeType> matched_needle_indexes;
        SizeType sidx = 0;
        SizeType idx = 0;
        for (const auto &match : matches) {
            if (current_line == 0) {
                current_line = match.line();    
            }
            if (current_line == match.line()) {
                matched_needle_indexes.insert(match.needle_index());
            }
            else {
                if (matched_needle_indexes.size() == needle_count) {
                    filtered_matches.insert(filtered_matches.end(), matches.begin() + sidx, matches.begin() + idx);
                }
                current_line = match.line();
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

    // return if all the matches got filtered out
    if (matches.size() == 0) {
        return results;
    }

    // merge spans if needed so each TextRef will contain all the matches for a line
    if (env.merge_spans() == MergeSpans::Yes) {
        std::vector<Match> filtered_matches;
        filtered_matches.reserve(matches.size());
        SizeType current_line = 0;
        for (auto &match : matches) {
            if (current_line == match.line()) {
                auto &back_match = filtered_matches.back();
                back_match.add_span(match.span());
            }
            else {
                current_line = match.line();
                filtered_matches.push_back(match);
            }
        }
        matches = filtered_matches;
        for (auto &match : matches) {
            match.simplify_span();
        }
    }

    if (env.mode() == Mode::Search) {
        // add a TextRef for each match
        for (auto &match : matches) {
            SizeType index = results.size() + 1;
            std::string line = std::string(source.substr(match.line_start_index(), match.line_length()));
            Span<SizeType> column_span;
            for (const auto &match_range : match.span().ranges()) {
                SizeType start_column = match_range.first() - match.line_start_index() + 1;
                SizeType end_column = match_range.last() - match.line_start_index() + 1;
                column_span.add(start_column, end_column);
            }
            results.push_back(TextRef(index, filename, match.line(), column_span, line));
        }
        return results;
    }

    ASSERT(env.mode() == Mode::SearchAndReplace || env.mode() == Mode::SearchAndReplaceDryRun);

    // set up a string to hold the new string after the search and replace operation
    // estimate the size by adding the length of the replacement for each match
    std::string output;
    output.reserve(source.length() + (matches.size() * env.replacement().length()));
    SizeType source_index = 0;
    std::string output_line;

    for (auto &match : matches) {
        // set up the source line and span for the replacement TextRef        
        std::string_view source_line = std::string_view(source.substr(match.line_start_index(), match.line_length()));
        output_line.clear();
        output_line.reserve(source_line.length() + (match.span().ranges().size() * env.replacement().length()));
        Span<SizeType> output_span;
        SizeType output_line_index = 0;
        
        for (const auto &match_range : match.span().ranges()) {
            // do the search and replace for the output file
            output += source.substr(source_index, match_range.first() - source_index);
            output += env.replacement();
            source_index += (match_range.first() - source_index);
            source_index += match_range.length();

            // do the search and replace for the TextRef       
            SizeType start_column = match_range.first() - match.line_start_index();
            output_line += source_line.substr(output_line_index, start_column - output_line_index);
            SizeType replacement_start_column = output_line.length() + 1;
            output_line += env.replacement();
            SizeType replacement_end_column = output_line.length() + 1;
            output_line_index += (start_column - output_line_index);
            output_line_index += match_range.length();
            output_span.add(replacement_start_column, replacement_end_column);
        }
        // append any remaining text on the output line
        output_line += source_line.substr(output_line_index);

        // make the TextRef with the replaced text
        SizeType index = results.size() + 1;
        results.push_back(TextRef(index, filename, match.line(), output_span, output_line));
    }
    // append any remaining text on the output file
    output += source.substr(source_index);

    // write the changed file if needed
    if (env.mode() == Mode::SearchAndReplace) {
        UU::write_file(filename, output);
    }

    return results;
}

static void output_refs(const Env &env, std::vector<TextRef> &refs) 
{
    std::sort(refs.begin(), refs.end(), std::less<TextRef>());

    UU::String output(2 * 1024 * 1024);

    int count = 1;
    for (auto &ref : refs) {
        ref.set_index(count);
        count++;
        int flags = (env.merge_spans() == MergeSpans::Yes) ? TextRef::CompactFeatures : TextRef::ExtendedFeatures;
        int highlight_color_value = static_cast<int>(env.highlight_color());
        output.append(ref.to_string(flags, TextRef::FilenameFormat::RELATIVE, env.current_path(), highlight_color_value));
        output += '\n';
    }
    std::cout << output;

    const char *refs_path = getenv("REFS_PATH");
    if (refs_path) {
        output.clear();
        std::ofstream file(refs_path);
        if (!file.fail()) {
            count = 1;
            for (auto &ref : refs) {
                ref.set_index(count);
                count++;
                output.append(ref.to_string(TextRef::StandardFeatures, TextRef::FilenameFormat::ABSOLUTE));
                output += '\n';
            }
            file << output;
        } 
    }
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
    puts("    -c <color>: Highlights results with the given color. Implies output to a terminal.");
    puts("                colors: black, gray, red, green, yellow, blue, magenta, cyan, white");
    puts(" ");
    puts("    -e : Search needles are compiles as regular expressions.");
    puts("    -h : Prints this help message.");
    puts("    -i : Case insensitive search.");
    puts("    -l : Show each found result on its own line.");
    puts("    -n : Search and replace dry run. Don't change any files. Ignored if not run with -r");
    puts("    -r : Search and replace. Takes two arguments: <search> <replacement>");
    puts("             <search> can be a string or a regex (when invoked with -e)");
    puts("             <replacement> is always treated as a string");
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
    {"long",              no_argument,       0, 'l'},
    {"dry-run",           no_argument,       0, 'n'},
    {"replace",           no_argument,       0, 'r'},
    {"search-skippables", no_argument,       0, 's'},
    {"version",           no_argument,       0, 'v'},
    {0, 0, 0, 0}
};

int main(int argc, char **argv)
{
    LOG_CHANNEL_ON(General);
    LOG_CHANNEL_ON(Error);

    bool option_a = false;
    bool option_e = false;
    bool option_i = false;
    bool option_l = false;
    bool option_n = false;
    bool option_r = false;
    bool option_s = false;

    std::string option_c;

    while (1) {
        int option_index = 0;
        int c = getopt_long(argc, argv, "ac:ehilnrsv", long_options, &option_index);
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
            case 'l':
                option_l = true;
                break;            
            case 'n':
                option_n = true;
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
            regex_needles.emplace_back(arg, regex_flags);
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
    Mode mode = Mode::Search;
    if (option_r) {
        mode = Mode::SearchAndReplace;
        if (option_n) {
            mode = Mode::SearchAndReplaceDryRun;
        }
    }
    HighlightColor highlight_color = HighlightColor::None;
    MergeSpans merge_spans = option_l ? MergeSpans::No : MergeSpans::Yes;
    if (option_c.length() > 0) {
        highlight_color = highlight_color_from_string(option_c);
        if (highlight_color == HighlightColor::None) {
            usage();
            std::cout << "\n*** unsupported highlight color: " << option_c << std::endl;
            exit(-1);
        }
    }

    fs::path current_path = fs::current_path();
    const auto &files = build_file_list(current_path, option_s ? Skip::SkipNone : Skip::SkipSkippables);

    Env env(fs::current_path(),
            string_needles,
            regex_needles,
            replacement,
            highlight_color,
            match_type,
            merge_spans,
            mode,
            search_case);

#if USE_DISPATCH
    dispatch_queue_t completion_queue = dispatch_queue_create("iota-search", DISPATCH_QUEUE_SERIAL);

    __block int completions = 0;
    __block std::vector<TextRef> all_refs;

    for (const auto &filename : files) {
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^{
            auto file_refs = process_file(filename, env);
            dispatch_async(completion_queue, ^{
                all_refs.insert(all_refs.end(), file_refs.begin(), file_refs.end());
                completions++;
                if (completions == files.size()) {
                    output_refs(env, all_refs);
                    exit(0);
                }
            });
        });
    }

    dispatch_main();
#else
    std::vector<std::future<std::vector<TextRef>>> futures;
    futures.reserve(files.size());
    for (const auto &filename : files) {
        auto a = std::async(std::launch::async, process_file, filename, env);
        futures.push_back(std::move(a));
    }

    std::vector<TextRef> all_refs;
    for (auto &f :futures) {
        std::vector<TextRef> file_refs = f.get();
        all_refs.insert(all_refs.cend(), file_refs.begin(), file_refs.end());
    }

    output_refs(env, all_refs);
#endif

    return 0;
}
