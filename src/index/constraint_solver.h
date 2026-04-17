// constraint_solver.h
#pragma once

#include "index.h"
#include "isr.h"
#include <vector>
#include <string>

class ConstraintSolver {
private:
    Index* index;
    
public:
    ConstraintSolver(Index* idx);
    
    // AND query - find documents containing ALL terms
    std::vector<int> FindAndQuery(const std::vector<std::string>& terms);
    
    // OR query - find documents containing ANY term
    std::vector<int> FindOrQuery(const std::vector<std::string>& terms);
};
