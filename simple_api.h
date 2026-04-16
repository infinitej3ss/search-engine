// simple_api.h
#pragma once

#ifdef _WIN32
    #ifdef BUILDING_DLL
        #define API __declspec(dllexport)
    #else
        #define API __declspec(dllimport)
    #endif
#else
    #define API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void* IndexHandle;

// Create/destroy
API IndexHandle create_index();
API void destroy_index(IndexHandle handle);

// Building index
API int add_document(IndexHandle handle, const char* url, 
                     const char** titlewords, int title_count,
                     const char** bodywords, int body_count,
                     int distance);
API int finalize_index(IndexHandle handle);
API int build_index_from_crawler(IndexHandle handle);

// Queries
API int* find_and_query(IndexHandle handle, const char** terms, int num_terms, int* result_count);
API void free_results(int* results);

// Metadata
API const char* get_document_url(IndexHandle handle, int doc_id);
API int get_document_count(IndexHandle handle);
API int get_document_frequency(IndexHandle handle, const char* term);

#ifdef __cplusplus
}
#endif
