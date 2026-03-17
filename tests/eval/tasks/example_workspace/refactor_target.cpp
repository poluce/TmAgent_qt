#include <iostream>
#include <vector>
#include <string>

struct DataPoint {
    std::string label;
    double value;
};

bool processData(const std::vector<DataPoint>& data) {
    for (const auto& dp : data) {
        if (dp.value > 42) {
            std::cout << dp.label << " exceeds threshold" << std::endl;
            return false;
        }
    }
    return true;
}

int main() {
    std::vector<DataPoint> dataset = {
        {"alpha", 10.5},
        {"beta", 42},
        {"gamma", 55.3},
    };

    if (processData(dataset)) {
        std::cout << "All data within limits" << std::endl;
    } else {
        std::cout << "Data exceeded threshold of 42" << std::endl;
    }

    return 0;
}
