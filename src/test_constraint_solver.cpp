// g++ -std=c++17 test_constraint_solver.cpp index.cpp Common.cpp page_data.cpp isr.cpp constraint_solver.cpp -o test_constraint_solver
// ./test_constraint_solver
#include "index.h"
#include "page_data.h"
#include "isr.h"
#include "constraint_solver.h"
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

using namespace std;

static int tests_run = 0;
static int tests_failed = 0;

static string to_str(const vector<int>& v) {
    string s = "[";
    for (size_t i = 0; i < v.size(); i++) {
        if (i) s += ", ";
        s += to_string(v[i]);
    }
    return s + "]";
}

static void expect_eq(const string& name, vector<int> got, vector<int> want) {
    sort(got.begin(), got.end());
    sort(want.begin(), want.end());
    tests_run++;
    if (got == want) {
        cout << "  [PASS] " << name << " = " << to_str(got) << endl;
    } else {
        tests_failed++;
        cout << "  [FAIL] " << name << "\n         got  " << to_str(got)
             << "\n         want " << to_str(want) << endl;
    }
}

static PageData make_page(const string& url,
                          const vector<string>& title,
                          const vector<string>& body) {
    PageData p;
    p.url = url;
    p.titlewords = title;
    p.words = body;
    p.anchor_text = {};
    return p;
}

int main() {
    Index* index = new Index();

    index->addDocument(make_page("https://example.com/cats",
        {"My", "Cat"},         {"the", "cat", "sat", "on", "the", "mat"}));      // doc 0
    index->addDocument(make_page("https://example.com/dogs",
        {"A", "Dog"},          {"the", "dog", "ran", "away"}));                  // doc 1
    index->addDocument(make_page("https://example.com/birds",
        {"Flying", "Bird"},    {"the", "bird", "flew"}));                        // doc 2
    index->addDocument(make_page("https://example.com/pets",
        {"Cat", "And", "Dog"}, {"the", "cat", "and", "the", "dog"}));            // doc 3
    index->addDocument(make_page("https://example.com/quickbird",
        {"Quick", "Bird"},     {"the", "quick", "bird"}));                       // doc 4

    index->Finalize();

    ConstraintSolver solver(index);

    cout << "=== AND queries ===" << endl;
    expect_eq("the AND cat",        solver.FindAndQuery({"the", "cat"}),    {0, 3});
    expect_eq("the AND bird",       solver.FindAndQuery({"the", "bird"}),   {2, 4});
    expect_eq("the AND dog",        solver.FindAndQuery({"the", "dog"}),    {1, 3});
    expect_eq("cat AND dog",        solver.FindAndQuery({"cat", "dog"}),    {3});
    expect_eq("the AND cat AND dog",solver.FindAndQuery({"the", "cat", "dog"}), {3});
    expect_eq("missing term",       solver.FindAndQuery({"the", "quokka"}), {});
    expect_eq("empty query",        solver.FindAndQuery({}),                {});

    cout << "\n=== OR queries ===" << endl;
    expect_eq("cat OR bird",        solver.FindOrQuery({"cat", "bird"}),    {0, 2, 3, 4});
    expect_eq("dog OR flew",        solver.FindOrQuery({"dog", "flew"}),    {1, 2, 3});
    expect_eq("missing OR cat",     solver.FindOrQuery({"quokka", "cat"}),  {0, 3});

    cout << "\n=== ISR basics ===" << endl;
    {
        ISR isr(index, "the");
        vector<int> docs_visited;
        while (isr.IsValid()) {
            int d = isr.GetCurrentDocId();
            if (d < 0) break;
            if (docs_visited.empty() || docs_visited.back() != d) docs_visited.push_back(d);
            if (!isr.Next()) break;
        }
        expect_eq("'the' visits every doc", docs_visited, {0, 1, 2, 3, 4});
    }
    {
        ISR isr(index, "the");
        tests_run++;
        if (isr.SkipToDoc(3) && isr.GetCurrentDocId() == 3) {
            cout << "  [PASS] SkipToDoc(3) lands on doc 3" << endl;
        } else {
            tests_failed++;
            cout << "  [FAIL] SkipToDoc(3) did not land on doc 3 (got doc "
                 << isr.GetCurrentDocId() << ")" << endl;
        }
    }
    {
        ISR isr(index, "the");
        tests_run++;
        if (!isr.SkipToDoc(99)) {
            cout << "  [PASS] SkipToDoc past end returns false" << endl;
        } else {
            tests_failed++;
            cout << "  [FAIL] SkipToDoc(99) should have returned false" << endl;
        }
    }
    {
        ISR isr(index, "quokka");
        tests_run++;
        if (!isr.IsValid()) {
            cout << "  [PASS] ISR on missing term is invalid" << endl;
        } else {
            tests_failed++;
            cout << "  [FAIL] ISR on missing term should be invalid" << endl;
        }
    }

    cout << "\n=== Summary ===" << endl;
    cout << tests_run - tests_failed << " / " << tests_run << " passed" << endl;

    delete index;
    return tests_failed == 0 ? 0 : 1;
}
