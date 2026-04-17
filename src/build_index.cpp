#include <cstdio>
#include <string>

#include "index/index.h"
#include "index/page_data.h"

// Reads crawler page files `crawled_page_rank_<r>_num_<n>` from cwd and
// writes one blob per rank: `index_rank_<r>.blob`.
int main() {
    int total_docs = 0;
    for (int rank = 0; rank < static_cast<int>(NUM_PAGE_FILE_RANKS); rank++) {
        Index idx;
        int fileNum = 0;
        int rank_docs = 0;

        while (true) {
            std::string filename = "crawled_page_rank_" + std::to_string(rank) +
                                   "_num_" + std::to_string(fileNum);
            if (load_page_file(filename) != 0) break;

            PageData page;
            while (get_next_page(page) != -1) {
                idx.addDocument(page);
                rank_docs++;
            }
            close_page_file();
            fileNum++;
        }

        idx.Finalize();

        if (rank_docs == 0) {
            std::printf("rank %d: no crawler files found, skipping\n", rank);
            continue;
        }

        std::string out = "index_rank_" + std::to_string(rank) + ".blob";
        if (!idx.WriteBlob(out)) {
            std::fprintf(stderr, "rank %d: failed to write %s\n", rank, out.c_str());
            return 1;
        }
        std::printf("rank %d: %d docs -> %s\n", rank, rank_docs, out.c_str());
        total_docs += rank_docs;
    }

    std::printf("done. %d docs indexed across %llu ranks.\n",
                total_docs,
                static_cast<unsigned long long>(NUM_PAGE_FILE_RANKS));
    return 0;
}
