#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_set>

#include "index/index.h"
#include "index/page_data.h"

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
        Index idx;
        int rank_docs = 0;

        for (auto filename : file_names[rank]) {
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
            }
            close_page_file();
        }
        idx.Finalize();

        if (rank_docs == 0) {
            std::printf("rank %d: no crawler files found, skipping\n", static_cast<int>(rank));
            continue;
        }

        std::string out = "index_rank_" + std::to_string(rank) + ".blob";
        if (!idx.WriteBlob(out)) {
            std::fprintf(stderr, "rank %d: failed to write %s\n", static_cast<int>(rank), out.c_str());
            return 1;
        }

        std::printf("rank %d: %d docs -> %s\n", static_cast<int>(rank), rank_docs, out.c_str());
        total_docs += rank_docs;
    }
    
    std::printf("done. %d docs indexed across %llu ranks.\n",
                total_docs,
                static_cast<unsigned long long>(NUM_PAGE_FILE_RANKS));
    return 0;
}
