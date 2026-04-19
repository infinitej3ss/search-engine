#pragma once
#include <cstring>
#include <vector>
#include "page_data.h"
#include "HashTable.h"

using namespace std;

class Index{
    public:
        //[0-2]decorations [31-3]delta 
        uint32_t encodePost(char decoration, int delta){
            uint32_t dec_bits = 0;
            switch (decoration){
                case 'b'://body
                    dec_bits = 0;
                    break;
                case '@': //title
                    dec_bits = 1;
                    break;
                case '#': //url
                    dec_bits = 2;
                    break;
                case '$'://anchor text
                    dec_bits = 3;
                    break;
                case '%'://end of doc
                    dec_bits = 4;
                    break;
                default:
                    break;
            }
            return (delta<<3) | dec_bits;
        };//encode post

        uint32_t decodeDelta(uint32_t post){
            //shift to get rid of decoration bits and read bits as normal
            return post >> 3;
        }; //decode delta
        
        char decodeDecoration(uint32_t post){//chaged int return to char return
            uint32_t value = post & 0x7;
            switch (value){
                case 0:
                    return 'b';
                case 1:
                    return '@';
                case 2:
                    return '#';
                case 3:
                    return '$';
                case 4:
                    return '%';
                default:
                    return 'x';
            }
        
        };//decode decoration

        struct PostingList{
            //array of posts
            std::vector<uint32_t> posts;
            std::vector<int> seek_absolutes;
            std::vector<int> seek_indices;
            int last_abs_pos = 0;
            int num_docs = 0;
            int word_occurrences = 0; // Total occurrences of the term across all documents (not including the EOD post)
            int last_doc_id = -1; // Track the last document ID that was added for this term, to help with adding EOD markers correctly

            // Add constructor to ensure initialization
            PostingList() : last_abs_pos(0), num_docs(0), word_occurrences(0) {}
            
            void addPost(uint32_t encoded){
                posts.push_back(encoded);
            } // Add Post

            void addCheckpoint(int absolute_pos){
                seek_absolutes.push_back(absolute_pos);
                seek_indices.push_back((int)posts.size() - 1);
            }; //Add Checkpoint

            int translateDelta(int checkpoint_absolute_pos, int checkpoint_index, int target_index) const {
                int absolute = checkpoint_absolute_pos;
                for (int i = checkpoint_index; i <= target_index; i++){
                    absolute += posts[i] >> 3;
                }
                return absolute;
            };//Translate Deltas

            int findCheckpoint (int target_absolute_pos, int& out_absolute_pos, int& out_post_index)const{
                int lo = 0;
                int hi = (int)seek_absolutes.size() - 1;
                int best = -1;
                while(lo<=hi){
                    int mid = (lo + hi) / 2;
                    if (seek_absolutes[mid] <= target_absolute_pos){
                        best = mid;
                        lo = mid + 1;
                    }
                    else{
                        hi = mid - 1;
                    }
                }
                if (best == -1){
                    out_absolute_pos = 0;
                    out_post_index = 0;
                    return false;
                }
                out_absolute_pos = seek_absolutes[best];
                out_post_index = seek_indices[best];
                return true;
            };

        };//PostingList struct


        struct DocumentMetadata{
            int doc_id = 0;
            std::string url;
            std::vector<std::string> title_words;   // for display title + ranker title signals
            int hop_distance = -1;                  // from PageData::distance_from_seedlist
            int body_length = 0;                    // raw body word count (for BM25 avg/tf)
            int start_position = 0;
            int end_position = 0;
            // locator for fetching raw page data at query time (snippet gen)
            u_int64_t page_file_rank = 0;
            u_int64_t page_file_num = 0;
            u_int64_t page_file_index = 0;
        };

        Index();

        ~Index();
        void addDocument(const PageData& page);
        void Finalize();
        PostingList* getPostingList(const std::string& term);
        DocumentMetadata GetDocumentMetadata(int docId);
        int GetDocumentCount() const;
        int GetDocumentFrequency(const std::string& term) const;

        int GetBodyLength(int docId) const;

        int GetFieldTermFrequency(int docId, const std::string& term, char decoration) const;

        // absolute positions of posts for `term` within doc `docId` whose
        // decoration matches, in index order. span scoring uses differences
        // between positions, so absolute vs field-local doesn't matter
        std::vector<size_t> GetFieldPositions(int docId, const std::string& term,
                                              char decoration) const;

        bool WriteBlob(const std::string& path) const;
        bool LoadBlob(const std::string& path);
        //to deal with read only
        //const PostingList* getPostingList(const char* term)const;
        //debugging functions
        void DebugPositionMapping(int docId, int position);
        void DebugDocumentBoundaries();

        void EnableDebug(bool enable) { debug_mode = enable; }
        void SetDebugWord(const std::string& word) { debug_word = word; }
        
        vector<string> splitURL(const string& url){
            vector<string> parts;
            string current;
            
            //cout << "  splitURL input: '" << url << "'" << endl;
            
            for(char c : url){
                if(isalnum(c)){
                    current += tolower(c);
                } else if(!current.empty()){
                    //cout << "    Found part: '" << current << "'" << endl;
                    parts.push_back(current);
                    current.clear();
                }
            }
            if(!current.empty()){
            // cout << "    Found part: '" << current << "'" << endl;
                parts.push_back(current);
            }
            
            //cout << "    Total URL parts: " << parts.size() << endl;
            return parts;
        }       
        // Add this function to Index.h
    

        
    private:
        HashTable<std::string, PostingList> dictionary;

        std::vector<DocumentMetadata> documents;
        int globalPositionCounter = 0;

        void addPost(const std::string& term, char decoration, int docId);
        //debugging
        bool debug_mode = false;
        std::string debug_word;
    
    
};

// Build index from crawler data files
Index* BuildIndex();
