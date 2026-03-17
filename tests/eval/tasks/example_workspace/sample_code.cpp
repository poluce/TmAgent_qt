#include <iostream>
#include <vector>
#include <string>

// TODO: Add error handling for division by zero

class Calculator {
public:
    Calculator() : m_history() {}

    double add(double a, double b) {
        double result = a + b;
        m_history.push_back(result);
        return result;
    }

    double subtract(double a, double b) {
        double result = a - b;
        m_history.push_back(result);
        return result;
    }

    double multiply(double a, double b) {
        double result = a * b;
        m_history.push_back(result);
        return result;
    }

    // TODO: Implement divide method
    double divide(double a, double b) {
        return a / b;
    }

    std::vector<double> getHistory() const {
        return m_history;
    }

private:
    std::vector<double> m_history;
};

int main() {
    Calculator calc;
    std::cout << "2 + 3 = " << calc.add(2, 3) << std::endl;
    std::cout << "5 - 1 = " << calc.subtract(5, 1) << std::endl;
    std::cout << "4 * 6 = " << calc.multiply(4, 6) << std::endl;
    return 0;
}
