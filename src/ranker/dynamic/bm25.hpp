#pragma once

#include <unordered_map>
#include <string>
#include <vector>

#include "text.hpp"

class BM25 {
public:
    static constexpr double k = 1.2;
    static constexpr double b = 0.75;

    BM25(const std::vector<Text>& queries, const std::vector<Document>& docs);
    double score(const Document& doc, const Text& query, const size_t& n_of_docs) const;

private:
    void calc_avg_length(const std::vector<Document>& docs);
    void init_fqi_all(const std::vector<Text>& queries, const std::vector<Document>& docs);
    double idf(const std::string& qi, const size_t& n_of_docs) const;
    size_t fqi(const std::string& tok, const Document& doc) const;

    double avg_length = 0.0;
    std::unordered_map<std::string, size_t> fqi_all; // docs containing term qi
};
