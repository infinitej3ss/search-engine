#include <cstdio>
#include <string>

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
int main() {
    int total_docs = 0;
    std::vector<std::vector<std::string>> file_names = get_page_file_names("./");

    for (size_t rank = 0; rank < file_names.size(); rank++) {
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
