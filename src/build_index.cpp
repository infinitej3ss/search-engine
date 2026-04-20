#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <unordered_set>

#include "index/index_builder.h"
#include "index/page_data.h"

// read just the header of a crawler page file so we can sum expected doc
// counts before indexing. returns 0 on any error or bad magic so progress
// degrades gracefully rather than failing the build
static u_int64_t peek_num_pages(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return 0;
    PageFileHeader h{};
    ssize_t n = read(fd, &h, sizeof(h));
    close(fd);
    if (n != static_cast<ssize_t>(sizeof(h))) return 0;
    if (h.magic_number != CORRECT_MAGIC_NUMBER) return 0;
    return h.num_pages;
}

static std::string format_hms(double seconds) {
    if (seconds < 0 || !std::isfinite(seconds)) return "--:--:--";
    long s = static_cast<long>(seconds);
    long h = s / 3600;
    long m = (s % 3600) / 60;
    long sec = s % 60;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02ld:%02ld:%02ld", h, m, sec);
    return buf;
}

std::vector<std::vector<std::string>> get_page_file_names(const std::string& dir);

u_int64_t get_num_crawled_pages();

// find rank of a given page file name
//
// returns __INT64_MAX__ if the page is invalid
u_int64_t get_page_file_rank(const std::string& file_name);

// find num of a given page file name
//
// returns __INT64_MAX__ if the page is invalid
u_int64_t get_page_file_num(const std::string& file_name);



// Reads crawler page files `crawled_page_rank_<r>_num_<n>` from cwd and
// writes one blob per rank: `index_rank_<r>.blob`.
//
// Usage:
//   build_index                 - build all ranks (default)
//   build_index 2 3             - build only ranks 2 and 3
//   build_index 0-2             - build ranks 0, 1, 2 (inclusive range)
// useful when one rank previously segfaulted and you don't want to rebuild
// the ones that already succeeded.
int main(int argc, char** argv) {
    // parse rank selection from argv. empty set = all ranks
    std::unordered_set<size_t> selected_ranks;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        size_t dash = arg.find('-');
        if (dash != std::string::npos) {
            size_t lo = std::atoi(arg.substr(0, dash).c_str());
            size_t hi = std::atoi(arg.substr(dash + 1).c_str());
            for (size_t r = lo; r <= hi; r++) selected_ranks.insert(r);
        } else {
            selected_ranks.insert(static_cast<size_t>(std::atoi(arg.c_str())));
        }
    }

    int total_docs = 0;
    std::vector<std::vector<std::string>> file_names = get_page_file_names("./");

    for (size_t rank = 0; rank < file_names.size(); rank++) {
        if (!selected_ranks.empty() && selected_ranks.count(rank) == 0) {
            continue;
        }
        IndexBuilder idx;
        int rank_docs = 0;

        // pre-pass: sum expected docs so we can report progress + eta
        u_int64_t expected_docs = 0;
        for (const auto& filename : file_names[rank]) {
            expected_docs += peek_num_pages(filename);
        }
        std::printf("rank %zu: expecting ~%llu docs across %zu files\n",
                    rank,
                    static_cast<unsigned long long>(expected_docs),
                    file_names[rank].size());
        std::fflush(stdout);

        auto t_start = std::chrono::steady_clock::now();
        auto t_last_print = t_start;

        size_t file_idx = 0;
        for (auto filename : file_names[rank]) {
            file_idx++;
            u_int64_t file_pages = peek_num_pages(filename);
            std::fprintf(stderr, "\n[file %zu/%zu] %s (~%llu pages)\n",
                         file_idx, file_names[rank].size(), filename.c_str(),
                         static_cast<unsigned long long>(file_pages));
            std::fflush(stderr);
            if (load_page_file(filename) != 0) continue;

            u_int64_t file_num = get_page_file_num(filename);

            PageData page;
            int page_index;
            while ((page_index = get_next_page(page)) != -1) {
                page.page_file_rank = (u_int64_t)rank;
                page.page_file_num = file_num;
                page.page_file_index = (u_int64_t)page_index;
                idx.addDocument(page);
                rank_docs++;

                // chrono::now is cheap: check every doc so true stalls surface
                auto now = std::chrono::steady_clock::now();
                double since_print = std::chrono::duration<double>(now - t_last_print).count();
                if (since_print >= 2.0) {
                    double elapsed = std::chrono::duration<double>(now - t_start).count();
                    double rate = elapsed > 0 ? rank_docs / elapsed : 0;
                    double pct = expected_docs > 0
                                     ? 100.0 * rank_docs / static_cast<double>(expected_docs)
                                     : 0.0;
                    double eta = (expected_docs > 0 && rate > 0)
                                     ? (expected_docs - rank_docs) / rate
                                     : -1;
                    std::fprintf(stderr,
                                 "\rrank %zu: %d/%llu (%.1f%%) %.0f docs/s elapsed %s eta %s      ",
                                 rank,
                                 rank_docs,
                                 static_cast<unsigned long long>(expected_docs),
                                 pct,
                                 rate,
                                 format_hms(elapsed).c_str(),
                                 format_hms(eta).c_str());
                    std::fflush(stderr);
                    t_last_print = now;
                }
            }
            close_page_file();
        }
        std::fprintf(stderr, "\n");
        std::fflush(stderr);
        auto t_add_done = std::chrono::steady_clock::now();
        double add_secs = std::chrono::duration<double>(t_add_done - t_start).count();
        std::printf("rank %zu: added %d docs in %s, finalizing...\n",
                    rank, rank_docs, format_hms(add_secs).c_str());
        std::fflush(stdout);

        idx.Finalize();
        auto t_finalize_done = std::chrono::steady_clock::now();
        double finalize_secs = std::chrono::duration<double>(t_finalize_done - t_add_done).count();
        std::printf("rank %zu: finalize took %s\n",
                    rank, format_hms(finalize_secs).c_str());
        std::fflush(stdout);

        if (rank_docs == 0) {
            std::printf("rank %d: no crawler files found, skipping\n", static_cast<int>(rank));
            continue;
        }

        std::string out = "index_rank_" + std::to_string(rank) + ".blob";
        auto t_write_start = std::chrono::steady_clock::now();
        if (!idx.WriteBlobV4(out)) {
            std::fprintf(stderr, "rank %d: failed to write %s\n", static_cast<int>(rank), out.c_str());
            return 1;
        }
        double write_secs =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t_write_start).count();

        std::printf("rank %zu: %d docs -> %s (write %s)\n",
                    rank, rank_docs, out.c_str(), format_hms(write_secs).c_str());
        std::fflush(stdout);
        total_docs += rank_docs;
    }
    
    std::printf("done. %d docs indexed across %llu ranks.\n",
                total_docs,
                static_cast<unsigned long long>(NUM_PAGE_FILE_RANKS));
    return 0;
}
