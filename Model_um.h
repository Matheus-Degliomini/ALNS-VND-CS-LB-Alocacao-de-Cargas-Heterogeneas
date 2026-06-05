#ifndef MODEL_UM_H
#define MODEL_UM_H

#include "Instance_um.h"

#include "gurobi_c++.h"

#include <memory>
#include <ostream>
#include <string>
#include <vector>

class ModelUM {
public:
    explicit ModelUM(const InstanceUM& instance);

    void setLogFilePath(const std::string& log_file_path);
    void build();
    void optimize();
    void printSolutionSummary(std::ostream& out) const;
    void exportSolutionCsv(const std::string& output_path) const;

    double getObjectiveValue() const;
    double getBestBound() const;
    double getGap() const;
    int getStatus() const;

private:
    const InstanceUM& instance_;
    std::unique_ptr<GRBEnv> env_;
    std::unique_ptr<GRBModel> model_;
    std::string log_file_path_;

    std::vector<GRBVar> y_;
    std::vector<GRBVar> z_;
    std::vector<std::vector<std::vector<GRBVar>>> x_;
    std::vector<std::vector<std::vector<bool>>> x_exists_;

    bool built_ = false;

    void createVariables();
    void addObjective();
    void addConstraints();
    void ensureBuilt() const;
};

#endif
