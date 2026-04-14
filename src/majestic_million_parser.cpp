#include <fstream>
#include <iostream>
#include <vector>
#include <sstream>

using namespace std;

int main() {
    // File pointer
    fstream fin;
    fstream fout;

    // Open an existing file
    fin.open("crawler_test_files/majestic_million.csv", ios::in);
    fout.open("crawler_test_files/seedlist.txt", ios::out | ios::app);

    // Read the Data from the file
    // as String Vector
    vector<string> row;
    string line, word, temp;

    fin >> temp;
    int i = 0;
    while (fin >> temp) {
        row.clear();
        // used for breaking words
        stringstream s(temp);

        // read every column data of a row and
        // store it in a string variable, 'word'
        while (getline(s, word, ',')) {
            // add all the column data
            // of a row to a vector
            row.push_back(word);
        }

        fout << "https://" + row[2] << "/\n";
        i++;
        if(i > 250000) {
            break;
        }
    }

    fin.close();
    fout.close();
    return 0;
}